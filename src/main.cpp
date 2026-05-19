#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <JPEGDEC.h>
#include <PNGdec.h>
#include <Preferences.h>
#include <SD.h>
#include <SPI.h>
#include <WebServer.h>
#include <WiFi.h>

static constexpr int LCD_MISO = 5;
static constexpr int LCD_MOSI = 6;
static constexpr int LCD_SCLK = 7;
static constexpr int LCD_CS = 14;
static constexpr int LCD_DC = 15;
static constexpr int LCD_RST = 21;
static constexpr int LCD_BL = 22;
static constexpr int SD_CS = 4;

static constexpr int SCREEN_W = 320;
static constexpr int SCREEN_H = 172;
static constexpr int LCD_X_OFFSET = 0;
static constexpr int LCD_Y_OFFSET = 34;
static constexpr uint32_t SPI_HZ = 80000000;
static constexpr size_t PNG_LINE_PIXELS = 2048;
static constexpr uint8_t MAX_PAGES = 24;

static const char *ROOT_DIR = "/nameplate";
static const char *IMAGE_DIR = "/nameplate/images";
static const char *RENDER_DIR = "/nameplate/renders";
static const char *PAGES_PATH = "/nameplate/pages.json";
static const char *SETTINGS_PATH = "/nameplate/settings.json";

static constexpr uint16_t BLACK = 0x0000;
static constexpr uint16_t WHITE = 0xffff;
static constexpr uint16_t RED = 0xf800;
static constexpr uint16_t GREEN = 0x07e0;
static constexpr uint16_t BLUE = 0x001f;
static constexpr uint16_t CYAN = 0x07ff;
static constexpr uint16_t YELLOW = 0xffe0;
static constexpr uint16_t MAGENTA = 0xf81f;
static constexpr uint16_t PINK = 0xfc9f;
static constexpr uint16_t GRAY = 0x8410;

struct LcdInitCmd
{
  uint8_t cmd;
  const uint8_t *data;
  uint8_t len;
  uint16_t delayMs;
};

struct Page
{
  String type;
  String title;
  String path;
  uint32_t durationMs;
};

enum class PlayMode
{
  Auto,
  Manual,
};

static const uint8_t madctl[] = {0x60};
static const uint8_t pixfmt[] = {0x05};
static const uint8_t b0[] = {0x00, 0xe8};
static const uint8_t b2[] = {0x0c, 0x0c, 0x00, 0x33, 0x33};
static const uint8_t b7[] = {0x35};
static const uint8_t bb[] = {0x35};
static const uint8_t c0[] = {0x2c};
static const uint8_t c2[] = {0x01};
static const uint8_t c3[] = {0x13};
static const uint8_t c4[] = {0x20};
static const uint8_t c6[] = {0x0f};
static const uint8_t d0[] = {0xa4, 0xa1};
static const uint8_t d6[] = {0xa1};
static const uint8_t e0[] = {0xf0, 0x00, 0x04, 0x04, 0x04, 0x05, 0x29, 0x33, 0x3e, 0x38, 0x12, 0x12, 0x28, 0x30};
static const uint8_t e1[] = {0xf0, 0x07, 0x0a, 0x0d, 0x0b, 0x07, 0x28, 0x33, 0x3e, 0x36, 0x14, 0x14, 0x29, 0x32};

static const LcdInitCmd initCmds[] = {
    {0x11, nullptr, 0, 120},
    {0x36, madctl, sizeof(madctl), 0},
    {0x3a, pixfmt, sizeof(pixfmt), 0},
    {0xb0, b0, sizeof(b0), 0},
    {0xb2, b2, sizeof(b2), 0},
    {0xb7, b7, sizeof(b7), 0},
    {0xbb, bb, sizeof(bb), 0},
    {0xc0, c0, sizeof(c0), 0},
    {0xc2, c2, sizeof(c2), 0},
    {0xc3, c3, sizeof(c3), 0},
    {0xc4, c4, sizeof(c4), 0},
    {0xc6, c6, sizeof(c6), 0},
    {0xd0, d0, sizeof(d0), 0},
    {0xd6, d6, sizeof(d6), 0},
    {0xe0, e0, sizeof(e0), 0},
    {0xe1, e1, sizeof(e1), 0},
    {0x21, nullptr, 0, 0},
    {0x11, nullptr, 0, 120},
    {0x29, nullptr, 0, 20},
};

WebServer server(80);
Preferences prefs;
PNG png;
JPEGDEC jpeg;
File pngFile;
File jpegFile;
File uploadFile;

Page pages[MAX_PAGES];
uint8_t pageCount = 0;
uint8_t currentPage = 0;
PlayMode playMode = PlayMode::Auto;
uint32_t defaultDurationMs = 8000;
uint32_t lastPageChangeMs = 0;
bool sdReady = false;
bool apMode = false;
bool dirtyDisplay = true;
uint32_t restartAtMs = 0;
String lastUploadPath;

static uint16_t pngLine[PNG_LINE_PIXELS];
static uint8_t spiLine[SCREEN_W * 2];

const char INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>C6 电子铭牌</title>
<style>
body{margin:0;font-family:system-ui,-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif;background:#f5f7fb;color:#172033}
header{position:sticky;top:0;background:#111827;color:white;padding:12px 16px;z-index:2}
main{max-width:900px;margin:0 auto;padding:14px}
section{background:white;border:1px solid #d9e1ee;border-radius:8px;padding:14px;margin:12px 0}
h1{font-size:18px;margin:0} h2{font-size:16px;margin:0 0 12px}
label{display:block;font-size:13px;margin:10px 0 4px;color:#475569}
input,textarea,select,button{font:inherit}
input,textarea,select{width:100%;box-sizing:border-box;border:1px solid #cbd5e1;border-radius:6px;padding:9px;background:white}
textarea{min-height:88px}
button{border:0;border-radius:6px;padding:9px 12px;background:#2563eb;color:white;margin:4px 4px 4px 0}
button.secondary{background:#475569} button.danger{background:#dc2626}
.row{display:grid;grid-template-columns:1fr 1fr;gap:10px}
.pages{display:grid;gap:8px}.page{border:1px solid #d9e1ee;border-radius:8px;padding:10px;background:#fbfdff}
.muted{color:#64748b;font-size:13px}.status{font-size:13px;line-height:1.5}
canvas{width:100%;max-width:640px;aspect-ratio:320/172;border:1px solid #cbd5e1;border-radius:6px;background:#111}
@media(max-width:640px){.row{grid-template-columns:1fr}}
</style>
</head>
<body>
<header><h1>ESP32-C6 横板电子铭牌</h1></header>
<main>
<section><h2>状态</h2><div id="status" class="status">读取中...</div></section>
<section>
  <h2>Wi-Fi 配置</h2>
  <div class="row">
    <div><label>家里 Wi-Fi 名称</label><input id="ssid" placeholder="SSID"></div>
    <div><label>Wi-Fi 密码</label><input id="pass" type="password" placeholder="password"></div>
  </div>
  <button onclick="saveWifi()">保存 Wi-Fi 并重启</button>
  <p class="muted">如果设备连不上家里 Wi-Fi，会自动开热点 C6-Nameplate-XXXX。</p>
</section>
<section>
  <h2>播放控制</h2>
  <div class="row">
    <div><label>播放模式</label><select id="mode"><option value="auto">自动轮播</option><option value="manual">手动指定</option></select></div>
    <div><label>默认轮播秒数</label><input id="interval" type="number" min="2" value="8"></div>
  </div>
  <button onclick="saveDisplay()">应用播放设置</button>
</section>
<section>
  <h2>添加图片页</h2>
  <label>标题</label><input id="imageTitle" placeholder="例如：作品照片">
  <label>图片文件 JPG/PNG</label><input id="imageFile" type="file" accept="image/*">
  <button onclick="addImagePage()">上传并添加</button>
</section>
<section>
  <h2>添加文字/混合页</h2>
  <label>标题</label><input id="textTitle" placeholder="例如：我的铭牌">
  <label>文字内容</label><textarea id="textBody" placeholder="支持中文、英文、数字"></textarea>
  <div class="row">
    <div><label>文字颜色</label><input id="textColor" type="color" value="#ffffff"></div>
    <div><label>背景颜色</label><input id="bgColor" type="color" value="#111827"></div>
  </div>
  <label>可选背景图</label><input id="bgFile" type="file" accept="image/*">
  <canvas id="preview" width="320" height="172"></canvas><br>
  <button onclick="drawPreview()">预览</button>
  <button onclick="addTextPage()">渲染并添加</button>
</section>
<section>
  <h2>页面列表</h2>
  <div id="pages" class="pages"></div>
  <button onclick="savePages()">保存顺序</button>
</section>
</main>
<script>
let pages=[]; let state={current:0,mode:'auto',interval:8};
const W=320,H=172;
async function json(url,opt){const r=await fetch(url,opt); if(!r.ok)throw new Error(await r.text()); return await r.json();}
async function refresh(){
  state=await json('/api/status');
  document.getElementById('status').innerHTML =
    `IP：${state.ip || '-'}<br>模式：${state.apMode?'热点配网':'家里 Wi-Fi'}<br>SD：${state.sdReady?'正常':'不可用'}<br>当前页：${state.current+1}/${state.pageCount}`;
  document.getElementById('mode').value=state.mode;
  document.getElementById('interval').value=Math.round(state.intervalMs/1000);
  const p=await json('/api/pages'); pages=p.pages||[]; renderPages();
}
function renderPages(){
  const root=document.getElementById('pages'); root.innerHTML='';
  pages.forEach((p,i)=>{
    const el=document.createElement('div'); el.className='page';
    el.innerHTML=`<b>${i+1}. ${p.title||'(未命名)'}</b><div class="muted">${p.type||'image'} · ${p.path||''} · ${p.duration||8}s</div>`;
    const show=document.createElement('button'); show.textContent='显示'; show.onclick=()=>setCurrent(i);
    const up=document.createElement('button'); up.textContent='上移'; up.className='secondary'; up.onclick=()=>{if(i>0){[pages[i-1],pages[i]]=[pages[i],pages[i-1]];renderPages();}};
    const down=document.createElement('button'); down.textContent='下移'; down.className='secondary'; down.onclick=()=>{if(i<pages.length-1){[pages[i+1],pages[i]]=[pages[i],pages[i+1]];renderPages();}};
    const del=document.createElement('button'); del.textContent='删除'; del.className='danger'; del.onclick=async()=>{const old=pages.splice(i,1)[0]; if(old&&old.path)await deleteFile(old.path); await savePages();};
    el.append(show,up,down,del); root.appendChild(el);
  });
}
async function upload(endpoint,file){
  const fd=new FormData(); fd.append('file',file,file.name || 'render.jpg');
  return await json(endpoint,{method:'POST',body:fd});
}
async function addImagePage(){
  const f=document.getElementById('imageFile').files[0]; if(!f)return alert('请选择图片');
  const blob=await renderImageFile(f); blob.name='album.jpg';
  const r=await upload('/api/render',blob);
  pages.push({type:'image',title:document.getElementById('imageTitle').value||f.name,path:r.path,duration:Number(document.getElementById('interval').value)||8});
  await savePages();
}
function loadImage(file){return new Promise((res,rej)=>{const img=new Image();img.onload=()=>res(img);img.onerror=rej;img.src=URL.createObjectURL(file);});}
async function renderImageFile(file){
  const c=document.getElementById('preview'),ctx=c.getContext('2d');
  ctx.fillStyle='#000'; ctx.fillRect(0,0,W,H);
  const img=await loadImage(file); const s=Math.max(W/img.width,H/img.height); const iw=img.width*s,ih=img.height*s;
  ctx.drawImage(img,(W-iw)/2,(H-ih)/2,iw,ih);
  return await canvasBlob();
}
async function drawPreview(){
  const c=document.getElementById('preview'),ctx=c.getContext('2d');
  ctx.fillStyle=document.getElementById('bgColor').value; ctx.fillRect(0,0,W,H);
  const bg=document.getElementById('bgFile').files[0];
  if(bg){const img=await loadImage(bg); const s=Math.max(W/img.width,H/img.height); const iw=img.width*s,ih=img.height*s; ctx.drawImage(img,(W-iw)/2,(H-ih)/2,iw,ih);}
  const text=document.getElementById('textBody').value || 'Hello ESP32-C6';
  ctx.fillStyle=document.getElementById('textColor').value; ctx.textAlign='center'; ctx.textBaseline='middle';
  let size=40; ctx.font=`700 ${size}px system-ui,"Microsoft YaHei",sans-serif`;
  const lines=text.split(/\n/).flatMap(line=>wrap(ctx,line,W-28));
  while(lines.length*size*1.18>H-20 && size>16){size-=2;ctx.font=`700 ${size}px system-ui,"Microsoft YaHei",sans-serif`;}
  const top=H/2-(lines.length-1)*size*0.58;
  lines.forEach((line,i)=>ctx.fillText(line,W/2,top+i*size*1.18));
}
function wrap(ctx,text,maxW){const out=[];let line='';for(const ch of text){const test=line+ch;if(ctx.measureText(test).width>maxW&&line){out.push(line);line=ch}else line=test;} if(line)out.push(line); return out.length?out:[''];}
function canvasBlob(){return new Promise(res=>document.getElementById('preview').toBlob(res,'image/jpeg',0.9));}
async function addTextPage(){
  await drawPreview(); const blob=await canvasBlob(); blob.name='render.jpg';
  const r=await upload('/api/render',blob);
  pages.push({type:document.getElementById('bgFile').files[0]?'mixed':'text',title:document.getElementById('textTitle').value||'文字页',path:r.path,duration:Number(document.getElementById('interval').value)||8});
  await savePages();
}
async function savePages(){await json('/api/pages',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({pages})}); await refresh();}
async function deleteFile(path){try{await json('/api/delete',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({path})});}catch(e){}}
async function setCurrent(i){await json('/api/display',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({mode:'manual',current:i})}); await refresh();}
async function saveDisplay(){await json('/api/display',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({mode:document.getElementById('mode').value,interval:Number(document.getElementById('interval').value)||8,current:state.current})}); await refresh();}
async function saveWifi(){await json('/api/wifi',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({ssid:document.getElementById('ssid').value,pass:document.getElementById('pass').value})}); alert('已保存，设备即将重启');}
refresh().catch(e=>alert(e.message)); drawPreview();
</script>
</body>
</html>
)HTML";

bool endsWithIgnoreCase(const String &value, const char *suffix)
{
  String lower = value;
  lower.toLowerCase();
  return lower.endsWith(suffix);
}

bool isSupportedImage(const String &name)
{
  return endsWithIgnoreCase(name, ".png") ||
         endsWithIgnoreCase(name, ".jpg") ||
         endsWithIgnoreCase(name, ".jpeg");
}

String jsonEscape(const String &s)
{
  String out;
  for (size_t i = 0; i < s.length(); ++i)
  {
    char c = s[i];
    if (c == '"' || c == '\\')
    {
      out += '\\';
    }
    out += c;
  }
  return out;
}

void writeCommand(uint8_t cmd, const uint8_t *data = nullptr, size_t len = 0)
{
  SPI.beginTransaction(SPISettings(SPI_HZ, MSBFIRST, SPI_MODE0));
  digitalWrite(LCD_CS, LOW);
  digitalWrite(LCD_DC, LOW);
  SPI.transfer(cmd);
  if (data && len)
  {
    digitalWrite(LCD_DC, HIGH);
    for (size_t i = 0; i < len; ++i)
    {
      SPI.transfer(data[i]);
    }
  }
  digitalWrite(LCD_CS, HIGH);
  SPI.endTransaction();
}

void setAddressWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
  const uint16_t x0 = x + LCD_X_OFFSET;
  const uint16_t x1 = x + w - 1 + LCD_X_OFFSET;
  const uint16_t y0 = y + LCD_Y_OFFSET;
  const uint16_t y1 = y + h - 1 + LCD_Y_OFFSET;
  uint8_t col[] = {uint8_t(x0 >> 8), uint8_t(x0), uint8_t(x1 >> 8), uint8_t(x1)};
  uint8_t row[] = {uint8_t(y0 >> 8), uint8_t(y0), uint8_t(y1 >> 8), uint8_t(y1)};
  writeCommand(0x2a, col, sizeof(col));
  writeCommand(0x2b, row, sizeof(row));
  writeCommand(0x2c);
}

void sendPixelLine(const uint16_t *pixels, uint16_t count)
{
  for (uint16_t i = 0; i < count; ++i)
  {
    spiLine[i * 2] = uint8_t(pixels[i] >> 8);
    spiLine[i * 2 + 1] = uint8_t(pixels[i]);
  }
  digitalWrite(LCD_CS, LOW);
  digitalWrite(LCD_DC, HIGH);
  SPI.transferBytes(spiLine, nullptr, count * 2);
  digitalWrite(LCD_CS, HIGH);
}

void sendPixelLineRaw(const uint16_t *pixels, uint16_t count)
{
  digitalWrite(LCD_CS, LOW);
  digitalWrite(LCD_DC, HIGH);
  SPI.transferBytes(reinterpret_cast<uint8_t *>(const_cast<uint16_t *>(pixels)), nullptr, count * sizeof(uint16_t));
  digitalWrite(LCD_CS, HIGH);
}

void pushLineRGB565(int16_t x, int16_t y, uint16_t w, const uint16_t *pixels, bool rawPixels = false)
{
  if (y < 0 || y >= SCREEN_H || x >= SCREEN_W || x + int16_t(w) <= 0)
  {
    return;
  }
  if (x < 0)
  {
    pixels += -x;
    w += x;
    x = 0;
  }
  if (x + w > SCREEN_W)
  {
    w = SCREEN_W - x;
  }
  if (w == 0)
  {
    return;
  }

  setAddressWindow(x, y, w, 1);
  SPI.beginTransaction(SPISettings(SPI_HZ, MSBFIRST, SPI_MODE0));
  if (rawPixels)
  {
    sendPixelLineRaw(pixels, w);
  }
  else
  {
    sendPixelLine(pixels, w);
  }
  SPI.endTransaction();
}

void pushBlockRGB565(int16_t x, int16_t y, uint16_t w, uint16_t h, const uint16_t *pixels, bool rawPixels = false)
{
  if (w == 0 || h == 0 || x >= SCREEN_W || y >= SCREEN_H || x + int16_t(w) <= 0 || y + int16_t(h) <= 0)
  {
    return;
  }

  int16_t srcX = 0;
  int16_t srcY = 0;
  int16_t dstX = x;
  int16_t dstY = y;
  int16_t copyW = w;
  int16_t copyH = h;

  if (dstX < 0)
  {
    srcX = -dstX;
    copyW += dstX;
    dstX = 0;
  }
  if (dstY < 0)
  {
    srcY = -dstY;
    copyH += dstY;
    dstY = 0;
  }
  if (dstX + copyW > SCREEN_W)
  {
    copyW = SCREEN_W - dstX;
  }
  if (dstY + copyH > SCREEN_H)
  {
    copyH = SCREEN_H - dstY;
  }
  if (copyW <= 0 || copyH <= 0)
  {
    return;
  }

  for (int16_t row = 0; row < copyH; ++row)
  {
    pushLineRGB565(dstX, dstY + row, copyW, pixels + (srcY + row) * w + srcX, rawPixels);
  }
}

void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color)
{
  if (w <= 0 || h <= 0 || x >= SCREEN_W || y >= SCREEN_H || x + w <= 0 || y + h <= 0)
  {
    return;
  }
  if (x < 0)
  {
    w += x;
    x = 0;
  }
  if (y < 0)
  {
    h += y;
    y = 0;
  }
  if (x + w > SCREEN_W)
  {
    w = SCREEN_W - x;
  }
  if (y + h > SCREEN_H)
  {
    h = SCREEN_H - y;
  }

  uint16_t line[SCREEN_W];
  for (int16_t i = 0; i < w; ++i)
  {
    line[i] = color;
  }
  for (int16_t row = 0; row < h; ++row)
  {
    pushLineRGB565(x, y + row, w, line);
  }
}

void showColorBars()
{
  const int band = SCREEN_W / 7;
  fillRect(0, 0, band, SCREEN_H, WHITE);
  fillRect(band, 0, band, SCREEN_H, RED);
  fillRect(band * 2, 0, band, SCREEN_H, GREEN);
  fillRect(band * 3, 0, band, SCREEN_H, BLUE);
  fillRect(band * 4, 0, band, SCREEN_H, CYAN);
  fillRect(band * 5, 0, band, SCREEN_H, PINK);
  fillRect(band * 6, 0, SCREEN_W - band * 6, SCREEN_H, YELLOW);
}

void initLcd()
{
  pinMode(LCD_CS, OUTPUT);
  pinMode(SD_CS, OUTPUT);
  pinMode(LCD_DC, OUTPUT);
  pinMode(LCD_RST, OUTPUT);
  pinMode(LCD_BL, OUTPUT);
  digitalWrite(LCD_CS, HIGH);
  digitalWrite(SD_CS, HIGH);
  digitalWrite(LCD_DC, HIGH);
  ledcAttach(LCD_BL, 1000, 10);
  ledcWrite(LCD_BL, 800);

  SPI.begin(LCD_SCLK, LCD_MISO, LCD_MOSI, LCD_CS);
  digitalWrite(LCD_RST, LOW);
  delay(20);
  digitalWrite(LCD_RST, HIGH);
  delay(120);

  for (const auto &entry : initCmds)
  {
    writeCommand(entry.cmd, entry.data, entry.len);
    if (entry.delayMs)
    {
      delay(entry.delayMs);
    }
  }
}

bool ensureDir(const char *path)
{
  if (SD.exists(path))
  {
    return true;
  }
  return SD.mkdir(path);
}

void ensureStorage()
{
  if (!sdReady)
  {
    return;
  }
  ensureDir(ROOT_DIR);
  ensureDir(IMAGE_DIR);
  ensureDir(RENDER_DIR);
  if (!SD.exists(PAGES_PATH))
  {
    File f = SD.open(PAGES_PATH, FILE_WRITE);
    if (f)
    {
      f.print("{\"pages\":[]}");
      f.close();
    }
  }
  if (!SD.exists(SETTINGS_PATH))
  {
    File f = SD.open(SETTINGS_PATH, FILE_WRITE);
    if (f)
    {
      f.print("{\"mode\":\"auto\",\"current\":0,\"interval\":8}");
      f.close();
    }
  }
}

String findFirstImage(const char *directory, uint8_t depth = 0)
{
  File root = SD.open(directory);
  if (!root || !root.isDirectory())
  {
    return "";
  }
  File file = root.openNextFile();
  while (file)
  {
    String name = file.name();
    bool isDir = file.isDirectory();
    file.close();
    if (!isDir && isSupportedImage(name))
    {
      if (!name.startsWith("/"))
      {
        name = "/" + name;
      }
      root.close();
      return name;
    }
    if (isDir && depth < 1)
    {
      String childPath = name;
      if (!childPath.startsWith("/"))
      {
        childPath = "/" + childPath;
      }
      String found = findFirstImage(childPath.c_str(), depth + 1);
      if (found.length())
      {
        root.close();
        return found;
      }
    }
    file = root.openNextFile();
  }
  root.close();
  return "";
}

void loadSettings()
{
  if (!sdReady || !SD.exists(SETTINGS_PATH))
  {
    return;
  }
  File f = SD.open(SETTINGS_PATH, FILE_READ);
  if (!f)
  {
    return;
  }
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err)
  {
    return;
  }
  String mode = doc["mode"] | "auto";
  playMode = mode == "manual" ? PlayMode::Manual : PlayMode::Auto;
  currentPage = constrain(int(doc["current"] | 0), 0, MAX_PAGES - 1);
  defaultDurationMs = max(2, int(doc["interval"] | 8)) * 1000UL;
}

void saveSettings()
{
  if (!sdReady)
  {
    return;
  }
  File f = SD.open(SETTINGS_PATH, FILE_WRITE);
  if (!f)
  {
    return;
  }
  JsonDocument doc;
  doc["mode"] = playMode == PlayMode::Manual ? "manual" : "auto";
  doc["current"] = currentPage;
  doc["interval"] = defaultDurationMs / 1000;
  serializeJson(doc, f);
  f.close();
}

void loadPages()
{
  pageCount = 0;
  if (!sdReady || !SD.exists(PAGES_PATH))
  {
    return;
  }
  File f = SD.open(PAGES_PATH, FILE_READ);
  if (!f)
  {
    return;
  }
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err || !doc["pages"].is<JsonArray>())
  {
    Serial.printf("pages.json parse failed: %s\n", err.c_str());
    return;
  }
  for (JsonObject item : doc["pages"].as<JsonArray>())
  {
    if (pageCount >= MAX_PAGES)
    {
      break;
    }
    pages[pageCount].type = item["type"] | "image";
    pages[pageCount].title = item["title"] | "";
    pages[pageCount].path = item["path"] | "";
    pages[pageCount].durationMs = max(2, int(item["duration"] | 8)) * 1000UL;
    if (pages[pageCount].path.length())
    {
      pageCount++;
    }
  }
  if (currentPage >= pageCount && pageCount > 0)
  {
    currentPage = 0;
  }
}

void createFallbackPageFromSd()
{
  if (!sdReady || pageCount > 0)
  {
    return;
  }
  String image = findFirstImage("/");
  if (!image.length())
  {
    return;
  }
  pages[0] = {"image", "SD image", image, defaultDurationMs};
  pageCount = 1;
}

void *pngOpen(const char *filename, int32_t *size)
{
  pngFile = SD.open(filename);
  if (!pngFile)
  {
    *size = 0;
    return nullptr;
  }
  *size = pngFile.size();
  return &pngFile;
}

void pngClose(void *handle)
{
  (void)handle;
  if (pngFile)
  {
    pngFile.close();
  }
}

int32_t pngRead(PNGFILE *page, uint8_t *buffer, int32_t length)
{
  (void)page;
  return pngFile ? pngFile.read(buffer, length) : 0;
}

int32_t pngSeek(PNGFILE *page, int32_t position)
{
  (void)page;
  return pngFile ? pngFile.seek(position) : 0;
}

void pngDraw(PNGDRAW *draw)
{
  if (draw->iWidth > int(PNG_LINE_PIXELS))
  {
    return;
  }
  png.getLineAsRGB565(draw, pngLine, PNG_RGB565_BIG_ENDIAN, 0xffffffff);
  for (int i = 0; i < draw->iWidth; ++i)
  {
    pngLine[i] = (pngLine[i] >> 8) | (pngLine[i] << 8);
  }
  int x = (SCREEN_W - draw->iWidth) / 2;
  int y = draw->y + (SCREEN_H - png.getHeight()) / 2;
  pushLineRGB565(x, y, draw->iWidth, pngLine, true);
}

bool showPng(const String &path)
{
  Serial.printf("Showing PNG: %s\n", path.c_str());
  int16_t result = png.open(path.c_str(), pngOpen, pngClose, pngRead, pngSeek, pngDraw);
  if (result != PNG_SUCCESS)
  {
    Serial.printf("PNG open failed: %d\n", result);
    return false;
  }
  Serial.printf("PNG specs: %d x %d, %d bpp, pixel type %d\n", png.getWidth(), png.getHeight(), png.getBpp(), png.getPixelType());
  fillRect(0, 0, SCREEN_W, SCREEN_H, BLACK);
  result = png.decode(nullptr, 0);
  png.close();
  Serial.printf("PNG decode result: %d\n", result);
  return result == PNG_SUCCESS;
}

void *jpegOpen(const char *filename, int32_t *size)
{
  jpegFile = SD.open(filename);
  if (!jpegFile)
  {
    *size = 0;
    return nullptr;
  }
  *size = jpegFile.size();
  return &jpegFile;
}

void jpegClose(void *handle)
{
  (void)handle;
  if (jpegFile)
  {
    jpegFile.close();
  }
}

int32_t jpegRead(JPEGFILE *file, uint8_t *buffer, int32_t length)
{
  (void)file;
  return jpegFile ? jpegFile.read(buffer, length) : 0;
}

int32_t jpegSeek(JPEGFILE *file, int32_t position)
{
  (void)file;
  return jpegFile ? jpegFile.seek(position) : 0;
}

int jpegDraw(JPEGDRAW *draw)
{
  pushBlockRGB565(draw->x, draw->y, draw->iWidth, draw->iHeight, reinterpret_cast<uint16_t *>(draw->pPixels), true);
  return 1;
}

int chooseJpegScaleForCover(int w, int h, int &scaledW, int &scaledH)
{
  const int options[] = {JPEG_SCALE_EIGHTH, JPEG_SCALE_QUARTER, JPEG_SCALE_HALF, 0};
  const int divs[] = {8, 4, 2, 1};
  int fallback = 0;
  int fallbackDiv = 1;
  for (int i = 0; i < 4; ++i)
  {
    int sw = max(1, w / divs[i]);
    int sh = max(1, h / divs[i]);
    if (sw >= SCREEN_W && sh >= SCREEN_H)
    {
      scaledW = sw;
      scaledH = sh;
      return options[i];
    }
    fallback = options[i];
    fallbackDiv = divs[i];
  }
  scaledW = max(1, w / fallbackDiv);
  scaledH = max(1, h / fallbackDiv);
  return fallback;
}

bool showJpeg(const String &path)
{
  Serial.printf("Showing JPEG: %s\n", path.c_str());
  if (!jpeg.open(path.c_str(), jpegOpen, jpegClose, jpegRead, jpegSeek, jpegDraw))
  {
    Serial.println("JPEG decoder open failed");
    return false;
  }
  jpeg.setPixelType(RGB565_LITTLE_ENDIAN);
  int scaledW = jpeg.getWidth();
  int scaledH = jpeg.getHeight();
  int options = chooseJpegScaleForCover(scaledW, scaledH, scaledW, scaledH);
  int x = (SCREEN_W - scaledW) / 2;
  int y = (SCREEN_H - scaledH) / 2;
  Serial.printf("JPEG specs: %d x %d, scaled: %d x %d, option: %d\n", jpeg.getWidth(), jpeg.getHeight(), scaledW, scaledH, options);
  fillRect(0, 0, SCREEN_W, SCREEN_H, BLACK);
  int result = jpeg.decode(x, y, options);
  jpeg.close();
  Serial.printf("JPEG decode result: %d\n", result);
  return result == 1;
}

bool showImage(const String &path)
{
  if (!sdReady || !path.length())
  {
    return false;
  }
  if (endsWithIgnoreCase(path, ".png"))
  {
    return showPng(path);
  }
  if (endsWithIgnoreCase(path, ".jpg") || endsWithIgnoreCase(path, ".jpeg"))
  {
    return showJpeg(path);
  }
  return false;
}

void renderCurrentPage()
{
  dirtyDisplay = false;
  if (!sdReady)
  {
    fillRect(0, 0, SCREEN_W, SCREEN_H, RED);
    return;
  }
  if (pageCount == 0)
  {
    showColorBars();
    return;
  }
  if (currentPage >= pageCount)
  {
    currentPage = 0;
  }
  if (!showImage(pages[currentPage].path))
  {
    fillRect(0, 0, SCREEN_W, SCREEN_H, GRAY);
  }
  lastPageChangeMs = millis();
  saveSettings();
}

void handleSlideshow()
{
  if (playMode != PlayMode::Auto || pageCount < 2)
  {
    return;
  }
  uint32_t duration = pages[currentPage].durationMs ? pages[currentPage].durationMs : defaultDurationMs;
  if (millis() - lastPageChangeMs >= duration)
  {
    currentPage = (currentPage + 1) % pageCount;
    dirtyDisplay = true;
  }
}

String safeFilename(const String &original)
{
  String name;
  for (size_t i = 0; i < original.length(); ++i)
  {
    char c = original[i];
    if (isalnum(c) || c == '.' || c == '-' || c == '_')
    {
      name += c;
    }
    else if (c == ' ')
    {
      name += '_';
    }
  }
  if (!name.length())
  {
    name = "upload.bin";
  }
  return name;
}

String uploadFolder()
{
  return server.uri() == "/api/render" ? RENDER_DIR : IMAGE_DIR;
}

void handleUploadStream()
{
  if (!sdReady)
  {
    return;
  }
  HTTPUpload &upload = server.upload();
  if (upload.status == UPLOAD_FILE_START)
  {
    String folder = uploadFolder();
    String filename = safeFilename(upload.filename);
    lastUploadPath = folder + "/" + String(millis()) + "_" + filename;
    uploadFile = SD.open(lastUploadPath, FILE_WRITE);
    Serial.printf("Upload start: %s\n", lastUploadPath.c_str());
  }
  else if (upload.status == UPLOAD_FILE_WRITE)
  {
    if (uploadFile)
    {
      uploadFile.write(upload.buf, upload.currentSize);
    }
  }
  else if (upload.status == UPLOAD_FILE_END)
  {
    if (uploadFile)
    {
      uploadFile.close();
      Serial.printf("Upload done: %s, %u bytes\n", lastUploadPath.c_str(), upload.totalSize);
    }
  }
  else if (upload.status == UPLOAD_FILE_ABORTED)
  {
    if (uploadFile)
    {
      uploadFile.close();
    }
  }
}

void sendJsonDoc(JsonDocument &doc)
{
  String body;
  serializeJson(doc, body);
  server.send(200, "application/json", body);
}

void handleIndex()
{
  server.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
}

void handleStatus()
{
  JsonDocument doc;
  doc["sdReady"] = sdReady;
  doc["apMode"] = apMode;
  doc["ip"] = apMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
  doc["mode"] = playMode == PlayMode::Manual ? "manual" : "auto";
  doc["current"] = currentPage;
  doc["pageCount"] = pageCount;
  doc["intervalMs"] = defaultDurationMs;
  doc["ssid"] = WiFi.SSID();
  sendJsonDoc(doc);
}

void handlePagesGet()
{
  if (!sdReady || !SD.exists(PAGES_PATH))
  {
    server.send(200, "application/json", "{\"pages\":[]}");
    return;
  }
  File f = SD.open(PAGES_PATH, FILE_READ);
  server.streamFile(f, "application/json");
  f.close();
}

void handlePagesPost()
{
  if (!sdReady)
  {
    server.send(503, "application/json", "{\"error\":\"sd unavailable\"}");
    return;
  }
  String body = server.arg("plain");
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err || !doc["pages"].is<JsonArray>())
  {
    server.send(400, "application/json", "{\"error\":\"invalid pages json\"}");
    return;
  }
  File f = SD.open(PAGES_PATH, FILE_WRITE);
  if (!f)
  {
    server.send(500, "application/json", "{\"error\":\"cannot write pages\"}");
    return;
  }
  f.print(body);
  f.close();
  loadPages();
  if (currentPage >= pageCount && pageCount > 0)
  {
    currentPage = 0;
  }
  dirtyDisplay = true;
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleDisplayPost()
{
  String body = server.arg("plain");
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err)
  {
    server.send(400, "application/json", "{\"error\":\"invalid display json\"}");
    return;
  }
  String mode = doc["mode"] | (playMode == PlayMode::Manual ? "manual" : "auto");
  playMode = mode == "manual" ? PlayMode::Manual : PlayMode::Auto;
  if (doc["current"].is<int>() && pageCount > 0)
  {
    currentPage = constrain(int(doc["current"]), 0, int(pageCount) - 1);
  }
  if (doc["interval"].is<int>())
  {
    defaultDurationMs = max(2, int(doc["interval"])) * 1000UL;
  }
  saveSettings();
  dirtyDisplay = true;
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleWifiPost()
{
  String body = server.arg("plain");
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err)
  {
    server.send(400, "application/json", "{\"error\":\"invalid wifi json\"}");
    return;
  }
  String ssid = doc["ssid"] | "";
  String pass = doc["pass"] | "";
  if (!ssid.length())
  {
    server.send(400, "application/json", "{\"error\":\"ssid required\"}");
    return;
  }
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  server.send(200, "application/json", "{\"ok\":true,\"restart\":true}");
  restartAtMs = millis() + 1200;
}

void handleUploadDone()
{
  if (!sdReady)
  {
    server.send(503, "application/json", "{\"error\":\"sd unavailable\"}");
    return;
  }
  if (!lastUploadPath.length())
  {
    server.send(400, "application/json", "{\"error\":\"no file uploaded\"}");
    return;
  }
  JsonDocument doc;
  doc["ok"] = true;
  doc["path"] = lastUploadPath;
  sendJsonDoc(doc);
}

void handleDeletePost()
{
  if (!sdReady)
  {
    server.send(503, "application/json", "{\"error\":\"sd unavailable\"}");
    return;
  }
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, server.arg("plain"));
  if (err)
  {
    server.send(400, "application/json", "{\"error\":\"invalid delete json\"}");
    return;
  }
  String path = doc["path"] | "";
  if (!(path.startsWith(IMAGE_DIR) || path.startsWith(RENDER_DIR)))
  {
    server.send(400, "application/json", "{\"error\":\"refusing path\"}");
    return;
  }
  bool removed = !SD.exists(path) || SD.remove(path);
  server.send(removed ? 200 : 500, "application/json", removed ? "{\"ok\":true}" : "{\"error\":\"remove failed\"}");
}

void handleNotFound()
{
  server.send(404, "application/json", "{\"error\":\"not found\"}");
}

void startWebServer()
{
  server.on("/", HTTP_GET, handleIndex);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/pages", HTTP_GET, handlePagesGet);
  server.on("/api/pages", HTTP_POST, handlePagesPost);
  server.on("/api/display", HTTP_POST, handleDisplayPost);
  server.on("/api/wifi", HTTP_POST, handleWifiPost);
  server.on("/api/upload", HTTP_POST, handleUploadDone, handleUploadStream);
  server.on("/api/render", HTTP_POST, handleUploadDone, handleUploadStream);
  server.on("/api/delete", HTTP_POST, handleDeletePost);
  if (sdReady)
  {
    server.serveStatic("/nameplate", SD, "/nameplate");
  }
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("HTTP server started");
}

void startAccessPoint()
{
  String suffix = String(uint32_t(ESP.getEfuseMac()), HEX);
  suffix.toUpperCase();
  String apName = "C6-Nameplate-" + suffix.substring(max(0, int(suffix.length()) - 4));
  WiFi.mode(WIFI_AP);
  WiFi.softAP(apName.c_str());
  apMode = true;
  Serial.printf("AP mode: %s, IP: %s\n", apName.c_str(), WiFi.softAPIP().toString().c_str());
}

void startWifi()
{
  String ssid = prefs.isKey("ssid") ? prefs.getString("ssid", "") : "";
  String pass = prefs.isKey("pass") ? prefs.getString("pass", "") : "";
  if (ssid.length())
  {
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), pass.c_str());
    Serial.printf("Connecting to Wi-Fi: %s\n", ssid.c_str());
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 15000)
    {
      delay(250);
      Serial.print(".");
    }
    Serial.println();
    if (WiFi.status() == WL_CONNECTED)
    {
      apMode = false;
      Serial.printf("STA mode IP: %s\n", WiFi.localIP().toString().c_str());
      if (MDNS.begin("c6-nameplate"))
      {
        Serial.println("mDNS: http://c6-nameplate.local/");
      }
      return;
    }
    Serial.println("Wi-Fi failed, falling back to AP setup");
  }
  startAccessPoint();
}

void setup()
{
  Serial.begin(115200);
  delay(400);
  Serial.println();
  Serial.println("ESP32-C6-LCD-1.47 nameplate + album");

  prefs.begin("nameplate", false);
  initLcd();
  showColorBars();
  delay(900);

  digitalWrite(LCD_CS, HIGH);
  digitalWrite(SD_CS, HIGH);
  sdReady = SD.begin(SD_CS, SPI, SPI_HZ, "/sd", 5, true);
  if (sdReady)
  {
    Serial.printf("SD ready: total %llu MB, used %llu MB\n", SD.totalBytes() / 1024 / 1024, SD.usedBytes() / 1024 / 1024);
    ensureStorage();
    loadSettings();
    loadPages();
    createFallbackPageFromSd();
  }
  else
  {
    Serial.println("SD card initialization failed");
  }

  startWifi();
  startWebServer();
  dirtyDisplay = true;
}

void loop()
{
  server.handleClient();
  handleSlideshow();
  if (dirtyDisplay)
  {
    renderCurrentPage();
  }
  if (restartAtMs && millis() >= restartAtMs)
  {
    ESP.restart();
  }
  delay(2);
}
