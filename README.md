# ESP32-C6 横板电子铭牌 + 相册

这是一个基于 Waveshare ESP32-C6-LCD-1.47 开发板的横板电子铭牌/相册固件。它可以在板载 1.47 英寸 LCD 上显示 SD 卡中的图片或网页渲染出的文字页面，并通过局域网网页进行内容管理。


## 功能

- ESP32-C6-LCD-1.47 屏幕驱动，逻辑显示尺寸为 `320 x 172` 横屏。
- SD 卡内容存储，页面配置保存在 `/nameplate/pages.json`。
- 首次启动或 Wi-Fi 连接失败时自动开启热点配网。
- 连接家里 Wi-Fi 后提供本地网页管理界面。
- 网页支持：
  - 上传图片页。
  - 生成中英文文字页。
  - 生成文字 + 背景图混合页。
  - 页面排序、删除、手动显示。
  - 自动轮播和手动指定模式切换。
- Wi-Fi 名称和密码存储在 ESP32 NVS/Preferences 中，不写入 SD 卡。
- JPEG 使用 `JPEGDEC`，PNG 使用 `PNGdec`。

## 硬件

目标硬件：

- Waveshare ESP32-C6-LCD-1.47
- 芯片：ESP32-C6FH4/FH8 系列
- 屏幕：ST7789，172 x 320，横屏使用为 320 x 172
- Micro SD 卡槽
- USB-C 下载和串口

已使用的引脚：

| 功能 | GPIO |
| --- | --- |
| LCD MISO | 5 |
| LCD MOSI | 6 |
| LCD SCLK | 7 |
| LCD CS | 14 |
| LCD DC | 15 |
| LCD RST | 21 |
| LCD BL | 22 |
| SD CS | 4 |

## 软件环境

- Windows + PowerShell
- PlatformIO
- Arduino framework for ESP32-C6
- pioarduino `platform-espressif32`

项目依赖：

- `JPEGDEC`
- `ArduinoJson`
- `PNGdec`，放在仓库的 `lib/PNGdec`

## 目录结构

```text
.
├── extra_path.py
├── platformio.ini
├── README.md
├── src/
│   └── main.cpp
└── lib/
    └── PNGdec/
```

SD 卡运行时目录：

```text
/nameplate/
├── pages.json
├── settings.json
├── images/
└── renders/
```

## 编译

在项目根目录运行：

```powershell
platformio run -e esp32c6_lcd
```

如果 `platformio` 不在 PATH 中，可以使用本机已安装的 PlatformIO Python 入口，例如：

```powershell
python -m platformio run -e esp32c6_lcd
```

## 烧录

默认串口是 `COM4`，见 `platformio.ini`：

```ini
upload_port = COM4
monitor_port = COM4
```

烧录：

```powershell
platformio run -e esp32c6_lcd -t upload
```

如果 Windows 终端因为 esptool 进度条编码报错，可以先设置：

```powershell
$env:PYTHONIOENCODING='utf-8'
```

## 使用方法

1. 准备一张 FAT32 格式的 Micro SD 卡。
2. 插入开发板 SD 卡槽。
3. 烧录固件并重启开发板。
4. 首次启动时，如果没有保存过 Wi-Fi，开发板会开启热点：

```text
C6-Nameplate-XXXX
```

5. 手机连接这个热点，浏览器打开：

```text
http://192.168.4.1
```

6. 在网页里填写家里 Wi-Fi 名称和密码，保存后设备会重启。
7. 设备加入家里 Wi-Fi 后，串口会输出 IP，例如：

```text
STA mode IP: 192.168.31.30
```

8. 手机连接同一个家里 Wi-Fi，打开该 IP：

```text
http://192.168.31.30
```

也可以尝试：

```text
http://c6-nameplate.local/
```

## 网页管理

网页中可以完成：

- 查看设备状态和 IP。
- 配置 Wi-Fi。
- 设置自动轮播或手动显示。
- 上传图片并添加为页面。
- 输入中英文文字并渲染为页面。
- 设置文字颜色和背景颜色。
- 选择背景图生成混合页面。
- 删除页面。
- 调整页面顺序。

## 显示与图片处理说明

当前固件保留了接近 Waveshare 官方示例的图片输出方式：

- PNG：使用 `PNG_RGB565_BIG_ENDIAN` 读取，再做 16 位字节交换。
- JPEG：使用 `RGB565_LITTLE_ENDIAN`。
- 不做红蓝通道互换。

网页上传的图片会先在浏览器端裁剪为 `320 x 172`，再保存为 JPG，降低 ESP32 解码和缩放压力。

## 已知限制

- 第一版只支持局域网访问，没有账号密码。
- 不包含云端同步和手机 App。
- 蓝牙暂未实现。
- Web UI 内嵌在固件中，固件体积接近默认 4MB 分区上限。
- 超大 PNG/JPG 可能解码较慢，建议通过网页上传，让浏览器先裁剪成屏幕尺寸。

## 常见问题

### 配好 Wi-Fi 后热点消失了，怎么进入页面？

这是正常现象。配好 Wi-Fi 后设备会加入家里 Wi-Fi，不再开热点。请看串口输出的 `STA mode IP`，用手机访问这个 IP。

### 设备连不上家里 Wi-Fi 怎么办？

如果连接失败，设备会回到热点模式。重新连接 `C6-Nameplate-XXXX` 并访问 `http://192.168.4.1` 重新配置。

### 网页删除页面后 SD 卡文件也会删吗？

网页会请求固件删除 `/nameplate/images/` 或 `/nameplate/renders/` 下的文件，并更新 `pages.json`。

## 开发笔记

这个项目涉及的基础知识包括：

- PlatformIO 工程结构
- ESP32-C6 Arduino 开发
- USB 串口烧录和串口日志
- SPI 总线
- ST7789 LCD 初始化
- RGB565 像素格式
- SD/FAT 文件系统
- Wi-Fi AP/STA 模式
- HTTP WebServer
- JSON 配置文件
- NVS/Preferences 持久化存储

## License

本项目代码可按 MIT License 使用。第三方库遵循各自许可证。
