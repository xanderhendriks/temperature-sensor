#include <zephyr/device.h>
#include <errno.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/fs/fs.h>
#include <ff.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/disk_access.h>
#include <zephyr/usb/usb_device.h>
#include <zephyr/drivers/rtc.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/fs/littlefs.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/storage/flash_map.h>
#include <time.h>

LOG_MODULE_REGISTER(temp_logger, LOG_LEVEL_INF);

#define DISK_NAME "RAM"
#define MOUNT_POINT "/RAM:"
#define INDEX_PATH MOUNT_POINT "/index.htm"

#define RAM_LOG_CAPACITY 2048

#define USB_RX_BUF_SIZE 256
#define USB_TX_BUF_SIZE 512

#if IS_ENABLED(CONFIG_USB_CDC_ACM)
static const struct device *cdc_dev;
#endif
static const struct device *rtc_dev;
static const struct device *hts221_dev;
static const struct device *sht31_dev;
static struct fs_mount_t fs_mnt;
static FATFS fat_fs;
static bool fs_mounted;
K_MUTEX_DEFINE(ram_log_lock);

/* LittleFS storage for persistent logging */
#define LFS_MOUNT_POINT "/lfs"
#define LOG_FILE_PATH   LFS_MOUNT_POINT "/log.bin"
static bool lfs_ready;

FS_LITTLEFS_DECLARE_DEFAULT_CONFIG(lfs_cfg);
static struct fs_mount_t lfs_mnt = {
    .type = FS_LITTLEFS,
    .fs_data = &lfs_cfg,
    .storage_dev = (void *)FIXED_PARTITION_ID(lfs_partition),
    .mnt_point = LFS_MOUNT_POINT,
};

struct __packed log_header {
    uint32_t head;
    uint32_t count;
};

struct __packed ram_log_entry {
    int64_t ts;       /* Unix epoch seconds, or uptime if RTC not set */
    float temp_c;
    float hum_pct;    /* Relative humidity % */
    float temp2_c;    /* SHT31 temperature */
    float hum2_pct;   /* SHT31 humidity */
};

static bool rtc_time_set;

/* Return current Unix epoch seconds if RTC is set, otherwise uptime seconds */
static int64_t get_timestamp(void)
{
    if (rtc_dev && rtc_time_set) {
        struct rtc_time t;
        if (rtc_get_time(rtc_dev, &t) == 0) {
            struct tm *tm = rtc_time_to_tm(&t);
            return (int64_t)mktime(tm);
        }
    }
    return k_uptime_get() / 1000;
}

static struct ram_log_entry ram_log[RAM_LOG_CAPACITY];
static uint32_t ram_log_head;
static uint32_t ram_log_count;



static const char index_html[] =
"<!doctype html>\n"
"<html>\n"
"<head>\n"
"  <meta charset=\"utf-8\">\n"
"  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
"  <title>Temperature &amp; Humidity Logger</title>\n"
"  <style>\n"
"    body { font-family: ui-sans-serif, system-ui, sans-serif; margin: 24px; }\n"
"    button { margin-right: 8px; padding: 8px 12px; }\n"
"    button:disabled { opacity: 0.5; }\n"
"    #status { font-weight: bold; margin: 8px 0; }\n"
"    .ok { color: #16a34a; } .err { color: #dc2626; }\n"
"    canvas { border: 1px solid #ddd; width: 100%; max-width: 760px; height: 380px; }\n"
"    pre { background: #f7f7f7; padding: 12px; max-height: 200px; overflow: auto; }\n"
"    .checks { margin: 10px 0; }\n"
"    .checks label { margin-right: 16px; cursor: pointer; }\n"
"    .checks input[type=checkbox] { margin-right: 4px; }\n"
"    .swatch { display: inline-block; width: 14px; height: 3px;\n"
"              vertical-align: middle; margin-right: 4px; }\n"
"    .range-row { margin: 10px 0; display: flex; flex-wrap: wrap;\n"
"                 align-items: center; gap: 8px; }\n"
"    .range-row label { font-size: 13px; }\n"
"    .range-row input[type=datetime-local] { padding: 4px 6px; font-size: 13px; }\n"
"    .range-row button { padding: 4px 10px; font-size: 13px; }\n"
"    .quick-btns button { padding: 4px 10px; font-size: 12px; margin: 2px; }\n"
"  </style>\n"
"</head>\n"
"<body>\n"
"  <h1>Temperature &amp; Humidity Logger</h1>\n"
"  <p id=\"status\"></p>\n"
"  <button id=\"connect\">Connect</button>\n"
"  <button id=\"settime\" disabled>Set Time</button>\n"
"  <button id=\"get\" disabled>Get Data</button>\n"
"  <button id=\"current\" disabled>Get Current</button>\n"
"  <button id=\"clear\" disabled>Clear Data</button>\n"
"  <div class=\"checks\">\n"
"    <label><input type=\"checkbox\" id=\"cb_ht\">"
"<span class=\"swatch\" style=\"background:#1d4ed8\"></span>HTS221 Temp</label>\n"
"    <label><input type=\"checkbox\" id=\"cb_hh\">"
"<span class=\"swatch\" style=\"background:#93c5fd\"></span>HTS221 Hum</label>\n"
"    <label><input type=\"checkbox\" id=\"cb_st\" checked>"
"<span class=\"swatch\" style=\"background:#dc2626\"></span>SHT31 Temp</label>\n"
"    <label><input type=\"checkbox\" id=\"cb_sh\" checked>"
"<span class=\"swatch\" style=\"background:#fca5a5\"></span>SHT31 Hum</label>\n"
"  </div>\n"
"  <div class=\"range-row\">\n"
"    <label>From</label>\n"
"    <input type=\"datetime-local\" id=\"dt_from\" step=\"60\">\n"
"    <label>To</label>\n"
"    <input type=\"datetime-local\" id=\"dt_to\" step=\"60\">\n"
"    <button id=\"applyRange\">Apply</button>\n"
"    <button id=\"resetRange\">Show All</button>\n"
"  </div>\n"
"  <div class=\"quick-btns\">\n"
"    <button data-hrs=\"1\">Last 1h</button>\n"
"    <button data-hrs=\"6\">Last 6h</button>\n"
"    <button data-hrs=\"24\">Last 24h</button>\n"
"    <button data-hrs=\"72\">Last 3d</button>\n"
"    <button data-hrs=\"168\">Last 7d</button>\n"
"  </div>\n"
"  <canvas id=\"chart\" width=\"760\" height=\"380\"></canvas>\n"
"  <pre id=\"log\"></pre>\n"
"  <script>\n"
"    const logEl = document.getElementById('log');\n"
"    const statusEl = document.getElementById('status');\n"
"    const canvas = document.getElementById('chart');\n"
"    const ctx = canvas.getContext('2d');\n"
"    const getBtn = document.getElementById('get');\n"
"    const curBtn = document.getElementById('current');\n"
"    const timeBtn = document.getElementById('settime');\n"
"    const clrBtn = document.getElementById('clear');\n"
"    const dtFrom = document.getElementById('dt_from');\n"
"    const dtTo   = document.getElementById('dt_to');\n"
"    let port, reader, textBuf = '', allPts = [];\n"
"\n"
"    const series = [\n"
"      { id:'cb_ht', key:'t1', color:'#1d4ed8', label:'HTS221 Temp', isTemp:true },\n"
"      { id:'cb_hh', key:'h1', color:'#93c5fd', label:'HTS221 Hum',  isTemp:false },\n"
"      { id:'cb_st', key:'t2', color:'#dc2626', label:'SHT31 Temp',  isTemp:true },\n"
"      { id:'cb_sh', key:'h2', color:'#fca5a5', label:'SHT31 Hum',   isTemp:false }\n"
"    ];\n"
"\n"
"    function redraw() { if (allPts.length) drawChart(); }\n"
"    series.forEach(s => document.getElementById(s.id).addEventListener('change', redraw));\n"
"\n"
"    function toLocal(epoch) {\n"
"      const d = new Date(epoch * 1000);\n"
"      const pad = n => String(n).padStart(2, '0');\n"
"      return d.getFullYear()+'-'+pad(d.getMonth()+1)+'-'+pad(d.getDate())\n"
"             +'T'+pad(d.getHours())+':'+pad(d.getMinutes());\n"
"    }\n"
"    function fromLocal(s) { return s ? Math.floor(new Date(s).getTime()/1000) : 0; }\n"
"\n"
"    document.getElementById('applyRange').addEventListener('click', redraw);\n"
"    document.getElementById('resetRange').addEventListener('click', () => {\n"
"      dtFrom.value = ''; dtTo.value = ''; redraw();\n"
"    });\n"
"    document.querySelectorAll('.quick-btns button').forEach(b => {\n"
"      b.addEventListener('click', () => {\n"
"        const hrs = Number(b.dataset.hrs);\n"
"        if (!allPts.length) return;\n"
"        const latest = allPts[allPts.length-1].t;\n"
"        dtFrom.value = toLocal(latest - hrs * 3600);\n"
"        dtTo.value   = toLocal(latest);\n"
"        redraw();\n"
"      });\n"
"    });\n"
"\n"
"    function status(msg, ok) {\n"
"      statusEl.textContent = msg;\n"
"      statusEl.className = ok ? 'ok' : 'err';\n"
"    }\n"
"    function append(text) { logEl.textContent += text; logEl.scrollTop = logEl.scrollHeight; }\n"
"\n"
"    if (!('serial' in navigator)) {\n"
"      status('Web Serial not supported. Use Chrome or Edge.', false);\n"
"      document.getElementById('connect').disabled = true;\n"
"    } else {\n"
"      status('Click Connect to pair with the device.', true);\n"
"    }\n"
"\n"
"    async function connect() {\n"
"      try {\n"
"        port = await navigator.serial.requestPort();\n"
"        await port.open({ baudRate: 115200 });\n"
"        reader = port.readable.getReader();\n"
"        readLoop();\n"
"        status('Connected.', true);\n"
"        [getBtn,curBtn,timeBtn,clrBtn].forEach(b => b.disabled = false);\n"
"      } catch (e) { status('Connect failed: ' + e.message, false); }\n"
"    }\n"
"\n"
"    async function readLoop() {\n"
"      try {\n"
"        while (true) {\n"
"          const { value, done } = await reader.read();\n"
"          if (done) break;\n"
"          const chunk = new TextDecoder().decode(value);\n"
"          textBuf += chunk;\n"
"          append(chunk);\n"
"          if (textBuf.includes('HTS221_Temp_C')) {\n"
"            parseCsv(textBuf);\n"
"            drawChart();\n"
"          }\n"
"        }\n"
"      } catch (e) { status('Read error: ' + e.message, false); }\n"
"    }\n"
"\n"
"    async function send(cmd) {\n"
"      try {\n"
"        textBuf = '';\n"
"        const writer = port.writable.getWriter();\n"
"        await writer.write(new TextEncoder().encode(cmd + '\\n'));\n"
"        writer.releaseLock();\n"
"      } catch (e) { status('Send failed: ' + e.message, false); }\n"
"    }\n"
"\n"
"    function parseCsv(csv) {\n"
"      const lines = csv.trim().split(/\\r?\\n/).slice(1);\n"
"      allPts = lines.map(l => l.split(',')).map(p => ({\n"
"        t: Number(p[0]), t1: Number(p[1]), h1: Number(p[2]),\n"
"        t2: Number(p[3]), h2: Number(p[4])\n"
"      })).filter(p => !isNaN(p.t) && !isNaN(p.t1));\n"
"      if (allPts.length && !dtFrom.value) {\n"
"        dtFrom.value = toLocal(allPts[0].t);\n"
"        dtTo.value   = toLocal(allPts[allPts.length-1].t);\n"
"      }\n"
"    }\n"
"\n"
"    function drawChart() {\n"
"      const fEpoch = fromLocal(dtFrom.value);\n"
"      const tEpoch = fromLocal(dtTo.value);\n"
"      let pts = allPts;\n"
"      if (fEpoch) pts = pts.filter(p => p.t >= fEpoch);\n"
"      if (tEpoch) pts = pts.filter(p => p.t <= tEpoch);\n"
"      const active = series.filter(s => document.getElementById(s.id).checked);\n"
"      ctx.clearRect(0, 0, canvas.width, canvas.height);\n"
"      if (pts.length < 2 || !active.length) return;\n"
"\n"
"      const W = canvas.width, H = canvas.height;\n"
"      const ml = 55, mr = 55, mt = 28, mb = 50;\n"
"      const pw = W - ml - mr, ph = H - mt - mb;\n"
"      ctx.font = '11px sans-serif';\n"
"\n"
"      const tempS = active.filter(s => s.isTemp);\n"
"      const humS  = active.filter(s => !s.isTemp);\n"
"\n"
"      function yRange(keys) {\n"
"        let all = [];\n"
"        keys.forEach(k => pts.forEach(p => all.push(p[k])));\n"
"        let mn = Math.min(...all), mx = Math.max(...all);\n"
"        if (mx - mn < 1) { mn -= 0.5; mx += 0.5; }\n"
"        return { mn, mx, rng: mx - mn };\n"
"      }\n"
"      const tr = tempS.length ? yRange(tempS.map(s=>s.key)) : null;\n"
"      const hr = humS.length  ? yRange(humS.map(s=>s.key))  : null;\n"
"\n"
"      const nTicks = 5;\n"
"      ctx.strokeStyle = '#e5e7eb'; ctx.lineWidth = 1;\n"
"      for (let i = 0; i <= nTicks; i++) {\n"
"        const y = mt + ph - (i/nTicks)*ph;\n"
"        ctx.beginPath(); ctx.moveTo(ml, y); ctx.lineTo(ml+pw, y); ctx.stroke();\n"
"        if (tr) {\n"
"          ctx.fillStyle = '#1d4ed8'; ctx.textAlign = 'right';\n"
"          ctx.fillText((tr.mn+(i/nTicks)*tr.rng).toFixed(1), ml-4, y+4);\n"
"        }\n"
"        if (hr) {\n"
"          ctx.fillStyle = '#16a34a'; ctx.textAlign = 'left';\n"
"          ctx.fillText((hr.mn+(i/nTicks)*hr.rng).toFixed(0)+'%', ml+pw+4, y+4);\n"
"        }\n"
"      }\n"
"\n"
"      const tMin = pts[0].t, tMax = pts[pts.length-1].t;\n"
"      const tRng = tMax - tMin || 1;\n"
"      const nX = Math.min(pts.length, 6);\n"
"      ctx.fillStyle = '#374151'; ctx.textAlign = 'center';\n"
"      for (let i = 0; i < nX; i++) {\n"
"        const idx = Math.round(i*(pts.length-1)/(nX-1));\n"
"        const x = ml + ((pts[idx].t-tMin)/tRng)*pw;\n"
"        const d = new Date(pts[idx].t*1000);\n"
"        ctx.fillText(d.toLocaleDateString([],{month:'short',day:'numeric'}), x, H-mb+16);\n"
"        ctx.fillText(d.toLocaleTimeString([],{hour:'2-digit',minute:'2-digit'}), x, H-mb+30);\n"
"        ctx.strokeStyle = '#e5e7eb';\n"
"        ctx.beginPath(); ctx.moveTo(x, mt); ctx.lineTo(x, mt+ph); ctx.stroke();\n"
"      }\n"
"\n"
"      function plotLine(key, range, color) {\n"
"        ctx.beginPath();\n"
"        pts.forEach((p, i) => {\n"
"          const x = ml + ((p.t-tMin)/tRng)*pw;\n"
"          const y = mt + ph - ((p[key]-range.mn)/range.rng)*ph;\n"
"          i === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y);\n"
"        });\n"
"        ctx.strokeStyle = color; ctx.lineWidth = 2; ctx.stroke();\n"
"      }\n"
"      active.forEach(s => plotLine(s.key, s.isTemp ? tr : hr, s.color));\n"
"\n"
"      ctx.font = '12px sans-serif'; ctx.textAlign = 'left';\n"
"      active.forEach((s, i) => {\n"
"        ctx.fillStyle = s.color;\n"
"        ctx.fillText('\\u2014 ' + s.label, ml + i*130, mt-10);\n"
"      });\n"
"    }\n"
"\n"
"    document.getElementById('connect').addEventListener('click', connect);\n"
"    timeBtn.addEventListener('click', () => {\n"
"      const epoch = Math.floor(Date.now() / 1000);\n"
"      send('SET_TIME ' + epoch);\n"
"      status('Setting device time to ' + new Date().toISOString(), true);\n"
"    });\n"
"    getBtn.addEventListener('click', () => send('GET_DATA'));\n"
"    curBtn.addEventListener('click', () => send('GET_CURRENT'));\n"
"    clrBtn.addEventListener('click', () => {\n"
"      if (confirm('Clear all logged data?')) send('CLEAR_DATA');\n"
"    });\n"
"  </script>\n"
"</body>\n"
"</html>\n";

static void cdc_write(const char *data, size_t len)
{
#if !IS_ENABLED(CONFIG_USB_CDC_ACM)
    ARG_UNUSED(data);
    ARG_UNUSED(len);
    return;
#else
    if (!cdc_dev) {
        return;
    }

    for (size_t i = 0; i < len; i++) {
        uart_poll_out(cdc_dev, data[i]);
    }
#endif
}

static int mount_fs(void)
{
    int rc = disk_access_init(DISK_NAME);
    if (rc != 0) {
        LOG_ERR("disk_access_init failed: %d", rc);
        return rc;
    }

    LOG_INF("Mounting disk '%s' at '%s'", DISK_NAME, MOUNT_POINT);

    fs_mnt.type = FS_FATFS;
    fs_mnt.fs_data = &fat_fs;
    fs_mnt.storage_dev = (void *)DISK_NAME;
    fs_mnt.mnt_point = MOUNT_POINT;

    rc = fs_mount(&fs_mnt);
    if (rc != 0) {
        LOG_ERR("fs_mount failed: %d", rc);
    } else {
        LOG_INF("fs_mount OK");
        fs_mounted = true;
    }

    return rc;
}

static void unmount_fs(void)
{
    if (!fs_mounted) {
        return;
    }

    if (fs_unmount(&fs_mnt) != 0) {
        LOG_ERR("fs_unmount failed");
    } else {
        fs_mounted = false;
        LOG_INF("fs_unmount OK");
    }
}

static void ensure_index_html(void)
{
    struct fs_dirent entry;
    int rc = fs_stat(INDEX_PATH, &entry);
    if (rc == 0) {
        return;
    }

    LOG_INF("Creating %s", INDEX_PATH);

    struct fs_file_t file;
    fs_file_t_init(&file);

    rc = fs_open(&file, INDEX_PATH, FS_O_CREATE | FS_O_WRITE);
    if (rc == 0) {
        fs_write(&file, index_html, sizeof(index_html) - 1);
        fs_close(&file);
        LOG_INF("Wrote default index.htm");
    } else {
        LOG_ERR("Failed to create index.html: %d", rc);
    }
}



static void lfs_persist_entry(void)
{
    if (!lfs_ready) {
        return;
    }
    struct fs_file_t f;
    fs_file_t_init(&f);
    int rc = fs_open(&f, LOG_FILE_PATH, FS_O_CREATE | FS_O_WRITE);
    if (rc < 0) {
        LOG_ERR("lfs persist open: %d", rc);
        return;
    }
    /* Write header */
    struct log_header hdr = { .head = ram_log_head, .count = ram_log_count };
    fs_write(&f, &hdr, sizeof(hdr));
    /* Write all entries */
    fs_write(&f, ram_log, RAM_LOG_CAPACITY * sizeof(struct ram_log_entry));
    fs_close(&f);
}

static void append_log(float temp_c, float hum_pct, float temp2_c, float hum2_pct)
{
    int64_t ts = get_timestamp();

    k_mutex_lock(&ram_log_lock, K_FOREVER);
    uint32_t idx = ram_log_head;
    ram_log[idx].ts = ts;
    ram_log[idx].temp_c = temp_c;
    ram_log[idx].hum_pct = hum_pct;
    ram_log[idx].temp2_c = temp2_c;
    ram_log[idx].hum2_pct = hum2_pct;
    ram_log_head = (ram_log_head + 1) % RAM_LOG_CAPACITY;
    if (ram_log_count < RAM_LOG_CAPACITY) {
        ram_log_count++;
    }
    lfs_persist_entry();
    k_mutex_unlock(&ram_log_lock);
}

static void read_hts221(float *temp_c, float *hum_pct)
{
    struct sensor_value val;

    if (hts221_dev) {
        sensor_sample_fetch(hts221_dev);
        sensor_channel_get(hts221_dev, SENSOR_CHAN_AMBIENT_TEMP, &val);
        *temp_c = (float)val.val1 + (float)val.val2 / 1000000.0f;
        sensor_channel_get(hts221_dev, SENSOR_CHAN_HUMIDITY, &val);
        *hum_pct = (float)val.val1 + (float)val.val2 / 1000000.0f;
        return;
    }
    /* Fallback: simulated */
    int64_t seconds = k_uptime_get() / 1000;
    *temp_c = 22.0f + (float)(seconds % 300) / 50.0f;
    *hum_pct = 45.0f + (float)(seconds % 600) / 100.0f;
}

static void read_sht31(float *temp_c, float *hum_pct)
{
    struct sensor_value val;

    if (sht31_dev) {
        sensor_sample_fetch(sht31_dev);
        sensor_channel_get(sht31_dev, SENSOR_CHAN_AMBIENT_TEMP, &val);
        *temp_c = (float)val.val1 + (float)val.val2 / 1000000.0f;
        sensor_channel_get(sht31_dev, SENSOR_CHAN_HUMIDITY, &val);
        *hum_pct = (float)val.val1 + (float)val.val2 / 1000000.0f;
        return;
    }
    *temp_c = 0.0f;
    *hum_pct = 0.0f;
}

static void send_log_data(void)
{
    cdc_write("Timestamp,HTS221_Temp_C,HTS221_Hum_pct,SHT31_Temp_C,SHT31_Hum_pct\n", 56);
    k_mutex_lock(&ram_log_lock, K_FOREVER);
    for (uint32_t i = 0; i < ram_log_count; i++) {
        uint32_t idx = (ram_log_head + RAM_LOG_CAPACITY - ram_log_count + i) % RAM_LOG_CAPACITY;
        char line[120];
        int len = snprintk(line, sizeof(line), "%lld,%.2f,%.1f,%.2f,%.1f\n",
                           (long long)ram_log[idx].ts,
                           (double)ram_log[idx].temp_c,
                           (double)ram_log[idx].hum_pct,
                           (double)ram_log[idx].temp2_c,
                           (double)ram_log[idx].hum2_pct);
        cdc_write(line, len);
    }
    k_mutex_unlock(&ram_log_lock);
}

static void send_current_reading(void)
{
    char buf[120];
    float temp1, hum1, temp2, hum2;
    read_hts221(&temp1, &hum1);
    read_sht31(&temp2, &hum2);
    int len = snprintk(buf, sizeof(buf),
                       "HTS221: %.2f C  %.1f %%RH | SHT31: %.2f C  %.1f %%RH\n",
                       (double)temp1, (double)hum1,
                       (double)temp2, (double)hum2);
    cdc_write(buf, len);
}

static void handle_command(const char *cmd)
{
    if (strncmp(cmd, "GET_DATA", 8) == 0) {
        send_log_data();
    } else if (strncmp(cmd, "GET_CURRENT", 11) == 0) {
        send_current_reading();
    } else if (strncmp(cmd, "INFO", 4) == 0) {
        char buf[96];
        int len = snprintk(buf, sizeof(buf), "Temp+Humidity Logger\nEntries: %u\n", ram_log_count);
        cdc_write(buf, len);
    } else if (strncmp(cmd, "CLEAR_DATA", 10) == 0) {
        k_mutex_lock(&ram_log_lock, K_FOREVER);
        ram_log_head = 0;
        ram_log_count = 0;
        if (lfs_ready) {
            fs_unlink(LOG_FILE_PATH);
        }
        k_mutex_unlock(&ram_log_lock);
        cdc_write("OK\n", 3);
    } else if (strncmp(cmd, "SET_TIME ", 9) == 0) {
        /* Expect Unix epoch seconds, e.g. "SET_TIME 1738944600" */
        int64_t epoch = 0;
        const char *p = cmd + 9;
        while (*p >= '0' && *p <= '9') {
            epoch = epoch * 10 + (*p - '0');
            p++;
        }
        if (epoch > 0 && rtc_dev) {
            time_t t = (time_t)epoch;
            struct tm *gm = gmtime(&t);
            struct rtc_time rt = {
                .tm_sec = gm->tm_sec,
                .tm_min = gm->tm_min,
                .tm_hour = gm->tm_hour,
                .tm_mday = gm->tm_mday,
                .tm_mon = gm->tm_mon,
                .tm_year = gm->tm_year,
                .tm_wday = gm->tm_wday,
                .tm_yday = gm->tm_yday,
                .tm_isdst = gm->tm_isdst,
            };
            int rc = rtc_set_time(rtc_dev, &rt);
            if (rc == 0) {
                rtc_time_set = true;
                char buf[48];
                int len = snprintk(buf, sizeof(buf),
                    "OK %04d-%02d-%02dT%02d:%02d:%02dZ\n",
                    gm->tm_year + 1900, gm->tm_mon + 1, gm->tm_mday,
                    gm->tm_hour, gm->tm_min, gm->tm_sec);
                cdc_write(buf, len);
                LOG_INF("RTC set to %04d-%02d-%02d %02d:%02d:%02d UTC",
                    gm->tm_year + 1900, gm->tm_mon + 1, gm->tm_mday,
                    gm->tm_hour, gm->tm_min, gm->tm_sec);
            } else {
                char buf[32];
                int len = snprintk(buf, sizeof(buf), "ERROR: rtc_set_time %d\n", rc);
                cdc_write(buf, len);
            }
        } else {
            cdc_write("ERROR: invalid time or no RTC\n", 30);
        }
    } else if (strncmp(cmd, "GET_TIME", 8) == 0) {
        if (rtc_dev && rtc_time_set) {
            struct rtc_time t;
            if (rtc_get_time(rtc_dev, &t) == 0) {
                char buf[48];
                int len = snprintk(buf, sizeof(buf),
                    "%04d-%02d-%02dT%02d:%02d:%02dZ\n",
                    t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                    t.tm_hour, t.tm_min, t.tm_sec);
                cdc_write(buf, len);
            } else {
                cdc_write("ERROR: rtc_get_time failed\n", 27);
            }
        } else {
            cdc_write("RTC not set\n", 12);
        }
    } else {
        cdc_write("ERROR: Unknown command\n", 23);
    }
}

#if IS_ENABLED(CONFIG_USB_CDC_ACM)
static void usb_thread(void)
{
    char rx_buf[USB_RX_BUF_SIZE];
    size_t rx_len = 0;

    /* Give USB enumeration time to complete */
    k_msleep(2000);

    LOG_INF("CDC ACM thread started");

    while (true) {
        uint8_t c;
        if (uart_poll_in(cdc_dev, &c) == 0) {
            if (c == '\n' || c == '\r') {
                rx_buf[rx_len] = '\0';
                if (rx_len > 0) {
                    handle_command(rx_buf);
                }
                rx_len = 0;
            } else if (rx_len < sizeof(rx_buf) - 1) {
                rx_buf[rx_len++] = (char)c;
            }
        }

        k_msleep(10);
    }
}
#endif

static void logger_thread(void)
{
    /* Wait until the RTC time has been set before logging */
    while (!rtc_time_set) {
        k_sleep(K_MSEC(500));
    }
    LOG_INF("RTC time set – logging started");

    while (true) {
        float temp1, hum1, temp2, hum2;
        read_hts221(&temp1, &hum1);
        read_sht31(&temp2, &hum2);
        append_log(temp1, hum1, temp2, hum2);
        k_sleep(K_SECONDS(60));
    }
}

#if IS_ENABLED(CONFIG_USB_CDC_ACM)
K_THREAD_DEFINE(usb_tid, 2048, usb_thread, NULL, NULL, NULL, 5, 0, 0);
#endif
K_THREAD_DEFINE(log_tid, 2048, logger_thread, NULL, NULL, NULL, 5, 0, 0);

int main(void)
{
    int rc;

#if IS_ENABLED(CONFIG_USB_CDC_ACM)
    cdc_dev = DEVICE_DT_GET_ONE(zephyr_cdc_acm_uart);
    if (!device_is_ready(cdc_dev)) {
        LOG_ERR("CDC ACM device not ready");
        return 0;
    }
#endif

    rtc_dev = DEVICE_DT_GET(DT_NODELABEL(rtc));
    if (!device_is_ready(rtc_dev)) {
        LOG_WRN("RTC device not ready – timestamps will use uptime");
        rtc_dev = NULL;
    } else {
        LOG_INF("RTC device ready");
    }

    hts221_dev = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(hts221));
    if (hts221_dev && device_is_ready(hts221_dev)) {
        LOG_INF("HTS221 temperature sensor ready");
    } else {
        LOG_WRN("HTS221 not ready – using simulated temperature");
        hts221_dev = NULL;
    }

    sht31_dev = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(sht31));
    if (sht31_dev && device_is_ready(sht31_dev)) {
        LOG_INF("SHT31 temperature/humidity sensor ready");
    } else {
        LOG_WRN("SHT31 not available");
        sht31_dev = NULL;
    }

    /* Enable USB early so the host sees the device during NVS restore */
    rc = usb_enable(NULL);
    if (rc != 0) {
        LOG_ERR("usb_enable failed: %d", rc);
        return 0;
    }

    /* --- LittleFS init & log restore --- */
    rc = fs_mount(&lfs_mnt);
    if (rc == 0) {
        lfs_ready = true;
        LOG_INF("LittleFS mounted at %s", LFS_MOUNT_POINT);

        /* Restore saved log entries */
        struct fs_file_t f;
        fs_file_t_init(&f);
        rc = fs_open(&f, LOG_FILE_PATH, FS_O_READ);
        if (rc == 0) {
            struct log_header hdr;
            ssize_t bytes = fs_read(&f, &hdr, sizeof(hdr));
            if (bytes == sizeof(hdr)
                && hdr.count <= RAM_LOG_CAPACITY
                && hdr.head  <  RAM_LOG_CAPACITY) {
                bytes = fs_read(&f, ram_log,
                                RAM_LOG_CAPACITY * sizeof(struct ram_log_entry));
                ram_log_head  = hdr.head;
                ram_log_count = hdr.count;
                LOG_INF("Restored %u log entries from flash", hdr.count);
            } else {
                LOG_INF("No saved log data – starting fresh");
            }
            fs_close(&f);
        } else {
            LOG_INF("No log file yet – starting fresh");
        }
    } else {
        LOG_ERR("LittleFS mount failed: %d", rc);
    }

    rc = mount_fs();
    if (rc == 0) {
        ensure_index_html();
        unmount_fs();
    }

    LOG_INF("Temp logger ready. Open USB drive and index.htm");
    return 0;
}
