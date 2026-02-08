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
#include <zephyr/fs/nvs.h>
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
static struct fs_mount_t fs_mnt;
static FATFS fat_fs;
static bool fs_mounted;
K_MUTEX_DEFINE(ram_log_lock);

/* NVS storage for persistent logging */
static struct nvs_fs nvs;
static bool nvs_ready;

#define NVS_META_ID    0
#define NVS_ENTRY_BASE 1   /* IDs 1 .. RAM_LOG_CAPACITY */

struct __packed log_meta {
    uint32_t head;
    uint32_t count;
};

struct __packed ram_log_entry {
    int64_t ts;       /* Unix epoch seconds, or uptime if RTC not set */
    float temp_c;
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
"  <title>Temperature Logger</title>\n"
"  <style>\n"
"    body { font-family: ui-sans-serif, system-ui, sans-serif; margin: 24px; }\n"
"    button { margin-right: 8px; padding: 8px 12px; }\n"
"    button:disabled { opacity: 0.5; }\n"
"    #status { font-weight: bold; margin: 8px 0; }\n"
"    .ok { color: #16a34a; } .err { color: #dc2626; }\n"
"    canvas { border: 1px solid #ddd; width: 100%%; max-width: 720px; height: 320px; }\n"
"    pre { background: #f7f7f7; padding: 12px; max-height: 240px; overflow: auto; }\n"
"  </style>\n"
"</head>\n"
"<body>\n"
"  <h1>Temperature Logger</h1>\n"
"  <p id=\"status\"></p>\n"
"  <button id=\"connect\">Connect</button>\n"
"  <button id=\"settime\" disabled>Set Time</button>\n"
"  <button id=\"get\" disabled>Get Data</button>\n"
"  <button id=\"current\" disabled>Get Current</button>\n"
"  <button id=\"clear\" disabled>Clear Data</button>\n"
"  <pre id=\"log\"></pre>\n"
"  <canvas id=\"chart\" width=\"720\" height=\"320\"></canvas>\n"
"  <script>\n"
"    const logEl = document.getElementById('log');\n"
"    const statusEl = document.getElementById('status');\n"
"    const canvas = document.getElementById('chart');\n"
"    const ctx = canvas.getContext('2d');\n"
"    const getBtn = document.getElementById('get');\n"
"    const curBtn = document.getElementById('current');\n"
"    const timeBtn = document.getElementById('settime');\n"
"    const clrBtn = document.getElementById('clear');\n"
"    let port, reader, textBuf = '';\n"
"\n"
"    function status(msg, ok) {\n"
"      statusEl.textContent = msg;\n"
"      statusEl.className = ok ? 'ok' : 'err';\n"
"    }\n"
"\n"
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
"        getBtn.disabled = false;\n"
"        curBtn.disabled = false;\n"
"        timeBtn.disabled = false;\n"
"        clrBtn.disabled = false;\n"
"      } catch (e) {\n"
"        status('Connect failed: ' + e.message, false);\n"
"      }\n"
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
"          if (textBuf.includes('Timestamp,Temperature_C')) {\n"
"            drawChart(textBuf);\n"
"          }\n"
"        }\n"
"      } catch (e) {\n"
"        status('Read error: ' + e.message, false);\n"
"      }\n"
"    }\n"
"\n"
"    async function send(cmd) {\n"
"      try {\n"
"        textBuf = '';\n"
"        const writer = port.writable.getWriter();\n"
"        await writer.write(new TextEncoder().encode(cmd + '\\n'));\n"
"        writer.releaseLock();\n"
"      } catch (e) {\n"
"        status('Send failed: ' + e.message, false);\n"
"      }\n"
"    }\n"
"\n"
"    function drawChart(csv) {\n"
"      const lines = csv.trim().split(/\\r?\\n/).slice(1);\n"
"      const points = lines.map(l => l.split(',')).map(p => ({\n"
"        t: Number(p[0]), v: Number(p[1])\n"
"      })).filter(p => !isNaN(p.t) && !isNaN(p.v));\n"
"      if (points.length < 2) return;\n"
"      const minV = Math.min(...points.map(p => p.v));\n"
"      const maxV = Math.max(...points.map(p => p.v));\n"
"      const range = maxV - minV || 1;\n"
"      ctx.clearRect(0, 0, canvas.width, canvas.height);\n"
"      ctx.beginPath();\n"
"      points.forEach((p, i) => {\n"
"        const x = (i / (points.length - 1)) * canvas.width;\n"
"        const y = canvas.height - ((p.v - minV) / range) * canvas.height;\n"
"        i === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y);\n"
"      });\n"
"      ctx.strokeStyle = '#1d4ed8';\n"
"      ctx.lineWidth = 2;\n"
"      ctx.stroke();\n"
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



static void nvs_persist_entry(uint32_t idx)
{
    if (!nvs_ready) {
        return;
    }
    nvs_write(&nvs, NVS_ENTRY_BASE + idx,
              &ram_log[idx], sizeof(struct ram_log_entry));
    struct log_meta meta = { .head = ram_log_head, .count = ram_log_count };
    nvs_write(&nvs, NVS_META_ID, &meta, sizeof(meta));
}

static void append_log(float temp_c)
{
    int64_t ts = get_timestamp();

    k_mutex_lock(&ram_log_lock, K_FOREVER);
    uint32_t idx = ram_log_head;
    ram_log[idx].ts = ts;
    ram_log[idx].temp_c = temp_c;
    ram_log_head = (ram_log_head + 1) % RAM_LOG_CAPACITY;
    if (ram_log_count < RAM_LOG_CAPACITY) {
        ram_log_count++;
    }
    nvs_persist_entry(idx);
    k_mutex_unlock(&ram_log_lock);
}

static float read_temperature(void)
{
    struct sensor_value val;

    if (hts221_dev) {
        sensor_sample_fetch(hts221_dev);
        sensor_channel_get(hts221_dev, SENSOR_CHAN_AMBIENT_TEMP, &val);
        return (float)val.val1 + (float)val.val2 / 1000000.0f;
    }
    /* Fallback: simulated */
    int64_t seconds = k_uptime_get() / 1000;
    return 22.0f + (float)(seconds % 300) / 50.0f;
}

static void send_log_data(void)
{
    cdc_write("Timestamp,Temperature_C\n", 25);
    k_mutex_lock(&ram_log_lock, K_FOREVER);
    for (uint32_t i = 0; i < ram_log_count; i++) {
        uint32_t idx = (ram_log_head + RAM_LOG_CAPACITY - ram_log_count + i) % RAM_LOG_CAPACITY;
        char line[64];
        int len = snprintk(line, sizeof(line), "%lld,%.2f\n",
                           (long long)ram_log[idx].ts,
                           (double)ram_log[idx].temp_c);
        cdc_write(line, len);
    }
    k_mutex_unlock(&ram_log_lock);
}

static void send_current_temperature(void)
{
    char buf[64];
    float temp = read_temperature();
    int len = snprintk(buf, sizeof(buf), "%.2f\n", (double)temp);
    cdc_write(buf, len);
}

static void handle_command(const char *cmd)
{
    if (strncmp(cmd, "GET_DATA", 8) == 0) {
        send_log_data();
    } else if (strncmp(cmd, "GET_CURRENT", 11) == 0) {
        send_current_temperature();
    } else if (strncmp(cmd, "INFO", 4) == 0) {
        char buf[96];
        int len = snprintk(buf, sizeof(buf), "Temp Logger\nEntries: %u\n", ram_log_count);
        cdc_write(buf, len);
    } else if (strncmp(cmd, "CLEAR_DATA", 10) == 0) {
        k_mutex_lock(&ram_log_lock, K_FOREVER);
        ram_log_head = 0;
        ram_log_count = 0;
        if (nvs_ready) {
            struct log_meta meta = { .head = 0, .count = 0 };
            nvs_write(&nvs, NVS_META_ID, &meta, sizeof(meta));
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
        float temp = read_temperature();
        append_log(temp);
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

    /* --- NVS init & log restore --- */
    nvs.flash_device = FIXED_PARTITION_DEVICE(nvs_partition);
    if (device_is_ready(nvs.flash_device)) {
        struct flash_pages_info fpi;
        nvs.offset = FIXED_PARTITION_OFFSET(nvs_partition);
        flash_get_page_info_by_offs(nvs.flash_device, nvs.offset, &fpi);
        nvs.sector_size  = fpi.size;   /* 4096 */
        nvs.sector_count = FIXED_PARTITION_SIZE(nvs_partition) / nvs.sector_size;

        rc = nvs_mount(&nvs);
        if (rc == 0) {
            nvs_ready = true;
            LOG_INF("NVS: %u sectors of %u bytes",
                    nvs.sector_count, nvs.sector_size);

            /* Restore saved log entries */
            struct log_meta meta;
            if (nvs_read(&nvs, NVS_META_ID, &meta, sizeof(meta))
                == sizeof(meta)
                && meta.count <= RAM_LOG_CAPACITY
                && meta.head  <  RAM_LOG_CAPACITY) {
                uint32_t restored = 0;
                for (uint32_t i = 0; i < meta.count; i++) {
                    uint32_t idx = (meta.head + RAM_LOG_CAPACITY
                                    - meta.count + i) % RAM_LOG_CAPACITY;
                    if (nvs_read(&nvs, NVS_ENTRY_BASE + idx,
                                 &ram_log[idx],
                                 sizeof(struct ram_log_entry))
                        == sizeof(struct ram_log_entry)) {
                        restored++;
                    }
                }
                ram_log_head  = meta.head;
                ram_log_count = restored;
                LOG_INF("Restored %u log entries from flash", restored);
            } else {
                LOG_INF("No saved log data – starting fresh");
            }
        } else {
            LOG_ERR("NVS mount failed: %d", rc);
        }
    } else {
        LOG_ERR("NVS flash device not ready");
    }

    rc = mount_fs();
    if (rc == 0) {
        ensure_index_html();
        unmount_fs();
    }

    rc = usb_enable(NULL);
    if (rc != 0) {
        LOG_ERR("usb_enable failed: %d", rc);
        return 0;
    }

    LOG_INF("Temp logger ready. Open USB drive and index.htm");
    return 0;
}
