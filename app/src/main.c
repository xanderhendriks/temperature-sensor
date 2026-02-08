#include <zephyr/device.h>
#include <errno.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/fs/fs.h>
#include <ff.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/disk_access.h>
#include <zephyr/usb/usb_device.h>

LOG_MODULE_REGISTER(temp_logger, LOG_LEVEL_INF);

#define DISK_NAME "RAM"
#define MOUNT_POINT "/RAM:"
#define INDEX_PATH MOUNT_POINT "/index.htm"
#define LOG_PATH MOUNT_POINT "/temp_log.csv"
#define MAX_LOG_ENTRIES 100000

#define RAM_LOG_CAPACITY 2048

#define USB_RX_BUF_SIZE 256
#define USB_TX_BUF_SIZE 512

#if IS_ENABLED(CONFIG_USB_CDC_ACM)
static const struct device *cdc_dev;
#endif
static struct fs_mount_t fs_mnt;
static FATFS fat_fs;
static bool fs_mounted;
K_MUTEX_DEFINE(ram_log_lock);

struct ram_log_entry {
    int64_t ts;
    float temp_c;
};

static struct ram_log_entry ram_log[RAM_LOG_CAPACITY];
static uint32_t ram_log_head;
static uint32_t ram_log_count;

static uint32_t entry_count;

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
"    canvas { border: 1px solid #ddd; width: 100%; max-width: 720px; height: 320px; }\n"
"    pre { background: #f7f7f7; padding: 12px; max-height: 240px; overflow: auto; }\n"
"  </style>\n"
"</head>\n"
"<body>\n"
"  <h1>Temperature Logger</h1>\n"
"  <p>Connect over Web Serial, then request data.</p>\n"
"  <button id=\"connect\">Connect</button>\n"
"  <button id=\"get\">Get Data</button>\n"
"  <button id=\"current\">Get Current</button>\n"
"  <pre id=\"log\"></pre>\n"
"  <canvas id=\"chart\" width=\"720\" height=\"320\"></canvas>\n"
"  <script>\n"
"    const logEl = document.getElementById('log');\n"
"    const canvas = document.getElementById('chart');\n"
"    const ctx = canvas.getContext('2d');\n"
"    let port; let reader; let textBuf = '';\n"
"\n"
"    function append(text) { logEl.textContent += text; logEl.scrollTop = logEl.scrollHeight; }\n"
"\n"
"    async function connect() {\n"
"      port = await navigator.serial.requestPort();\n"
"      await port.open({ baudRate: 115200 });\n"
"      reader = port.readable.getReader();\n"
"      readLoop();\n"
"      append('Connected.\n');\n"
"    }\n"
"\n"
"    async function readLoop() {\n"
"      while (true) {\n"
"        const { value, done } = await reader.read();\n"
"        if (done) break;\n"
"        const chunk = new TextDecoder().decode(value);\n"
"        textBuf += chunk;\n"
"        append(chunk);\n"
"        if (textBuf.includes('Timestamp,Temperature_C')) {\n"
"          drawChart(textBuf);\n"
"        }\n"
"      }\n"
"    }\n"
"\n"
"    async function send(cmd) {\n"
"      const writer = port.writable.getWriter();\n"
"      await writer.write(new TextEncoder().encode(cmd + '\\n'));\n"
"      writer.releaseLock();\n"
"    }\n"
"\n"
"    function drawChart(csv) {\n"
"      const lines = csv.trim().split(/\r?\n/).slice(1);\n"
"      const points = lines.map(line => line.split(',')).map(p => ({\n"
"        t: Number(p[0]), v: Number(p[1])\n"
"      })).filter(p => !Number.isNaN(p.t) && !Number.isNaN(p.v));\n"
"      if (points.length < 2) return;\n"
"\n"
"      const minV = Math.min(...points.map(p => p.v));\n"
"      const maxV = Math.max(...points.map(p => p.v));\n"
"      ctx.clearRect(0, 0, canvas.width, canvas.height);\n"
"      ctx.beginPath();\n"
"      points.forEach((p, i) => {\n"
"        const x = (i / (points.length - 1)) * canvas.width;\n"
"        const y = canvas.height - ((p.v - minV) / (maxV - minV)) * canvas.height;\n"
"        if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);\n"
"      });\n"
"      ctx.strokeStyle = '#1d4ed8';\n"
"      ctx.lineWidth = 2;\n"
"      ctx.stroke();\n"
"    }\n"
"\n"
"    document.getElementById('connect').addEventListener('click', connect);\n"
"    document.getElementById('get').addEventListener('click', () => send('GET_DATA'));\n"
"    document.getElementById('current').addEventListener('click', () => send('GET_CURRENT'));\n"
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

static void ensure_log_file(void)
{
    struct fs_dirent entry;
    int rc = fs_stat(LOG_PATH, &entry);
    if (rc == 0) {
        return;
    }

    LOG_INF("Creating %s", LOG_PATH);

    struct fs_file_t file;
    fs_file_t_init(&file);
    rc = fs_open(&file, LOG_PATH, FS_O_CREATE | FS_O_WRITE);
    if (rc == 0) {
        const char *header = "Timestamp,Temperature_C\n";
        fs_write(&file, header, strlen(header));
        fs_close(&file);
        entry_count = 0;
        LOG_INF("Created log file");
    } else {
        LOG_ERR("Failed to create log file: %d", rc);
    }
}

static void append_log(float temp_c)
{
    int64_t ts = k_uptime_get() / 1000;

    k_mutex_lock(&ram_log_lock, K_FOREVER);
    ram_log[ram_log_head].ts = ts;
    ram_log[ram_log_head].temp_c = temp_c;
    ram_log_head = (ram_log_head + 1) % RAM_LOG_CAPACITY;
    if (ram_log_count < RAM_LOG_CAPACITY) {
        ram_log_count++;
    }
    k_mutex_unlock(&ram_log_lock);

    if (!fs_mounted) {
        return;
    }

    struct fs_file_t file;
    char line[64];
    int len;

    fs_file_t_init(&file);
    if (fs_open(&file, LOG_PATH, FS_O_CREATE | FS_O_WRITE | FS_O_APPEND) != 0) {
        return;
    }

    len = snprintk(line, sizeof(line), "%lld,%.2f\n", (long long)ts, (double)temp_c);
    fs_write(&file, line, len);
    fs_close(&file);
    entry_count++;
}

static float read_temperature(void)
{
    int64_t seconds = k_uptime_get() / 1000;
    float base = 22.0f;
    float drift = (float)(seconds % 300) / 50.0f;
    return base + drift;
}

static void send_log_data(void)
{
    if (!fs_mounted) {
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
        return;
    }

    struct fs_file_t file;
    char buf[USB_TX_BUF_SIZE];
    int rc;

    fs_file_t_init(&file);
    rc = fs_open(&file, LOG_PATH, FS_O_READ);
    if (rc != 0) {
        const char *err = "ERROR: Failed to open log file\n";
        cdc_write(err, strlen(err));
        return;
    }

    while ((rc = fs_read(&file, buf, sizeof(buf))) > 0) {
        cdc_write(buf, rc);
    }

    fs_close(&file);
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
        uint32_t count = fs_mounted ? entry_count : ram_log_count;
        int len = snprintk(buf, sizeof(buf), "Temp Logger\nEntries: %u\n", count);
        cdc_write(buf, len);
    } else if (strncmp(cmd, "CLEAR_DATA", 10) == 0) {
        k_mutex_lock(&ram_log_lock, K_FOREVER);
        ram_log_head = 0;
        ram_log_count = 0;
        k_mutex_unlock(&ram_log_lock);

        if (!fs_mounted) {
            cdc_write("OK\n", 3);
            return;
        }

        struct fs_file_t file;
        fs_file_t_init(&file);
        fs_unlink(LOG_PATH);
        if (fs_open(&file, LOG_PATH, FS_O_CREATE | FS_O_WRITE) == 0) {
            const char *header = "Timestamp,Temperature_C\n";
            fs_write(&file, header, strlen(header));
            fs_close(&file);
            entry_count = 0;
            cdc_write("OK\n", 3);
        } else {
            cdc_write("ERROR: Failed to clear log\n", 28);
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

    rc = mount_fs();
    if (rc == 0) {
        ensure_index_html();
        ensure_log_file();
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
