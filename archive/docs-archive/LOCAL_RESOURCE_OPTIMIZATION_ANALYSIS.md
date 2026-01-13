# WeRead 本地资源优化分析

## 测试时间
2025-12-06

---

## 当前性能现状

### 测试结果（500MB HTTP缓存）
- **缓存命中加载时间**: 79.2秒 (loadStarted → loadFinished)
- **首次加载**: ~80秒
- **历史最佳**: 20-30秒
- **目标**: 20-30秒

---

## 性能瓶颈分析

### 关键发现
即使在**100%缓存命中**的情况下（`transferSize: 0` 所有资源），加载仍需79秒。

### 资源性能数据（Browser Performance API）

| 资源文件 | transferSize | dur (ms) | respEnd (ms) | 类型 |
|---------|--------------|----------|--------------|------|
| app.css | 0 | 337 | 1,485 | link |
| app.js | 0 | 2,109 | 3,259 | link |
| common.js | 0 | 9,780 | 12,446 | script |
| utils.js | 0 | 9,481 | 12,170 | script |
| loading.png | 0 | 1,861 | 57,552 | img |

### 分析结论

1. **`transferSize: 0`** 证实所有资源从HTTP磁盘缓存加载（无网络传输）
2. **即使命中缓存，`common.js` 仍需9.8秒加载**
3. **`utils.js` 从缓存加载需9.5秒**
4. **总资源加载时间约57秒，全部来自缓存**
5. **瓶颈**: 缓存读取/解析开销，而非网络延迟

**结论**: QtWebEngine在ARM64设备上的磁盘缓存读取性能是主要瓶颈，而非网络下载速度。

---

## 本地资源优化尝试

### ❌ 失败方案：直接文件重定向 (2025-12-06)

#### 实现代码
```cpp
void interceptRequest(QWebEngineUrlRequestInfo &info) override {
    const QString url = info.requestUrl().toString();

    // 重定向到本地文件
    if (url.contains("app.js")) {
        info.redirect(QUrl::fromLocalFile("/home/root/weread/local-assets/js/app.js"));
        return;
    }
}
```

#### 测试结果
- ✅ **loadFinished: 16.3秒** (vs 79秒，性能提升79.4%！)
- ❌ **屏幕完全空白** (白度 100%, 稳定60秒)
- ❌ JavaScript控制台无错误
- ❌ 所有资源加载成功但页面未渲染

#### 失败原因：CORS/跨域安全限制

QtWebEngine的 `info.redirect(QUrl::fromLocalFile())` 触发了**CORS跨域安全限制**：

1. **`file://` URL与 `https://` URL的origin不同**
2. **从 `file://` 加载的JavaScript无法访问从 `https://` 加载的DOM**
3. **浏览器安全模型阻止跨域脚本执行**
4. **结果**: 脚本加载但无法执行，导致页面空白

#### 经验教训
直接的 `file://` 重定向与Web安全模型不兼容。

---

## ✅ 正确的本地资源优化方案

### 方案1: 自定义URL Scheme Handler（推荐）

#### 实现代码
```cpp
// 1. 在QWebEngineProfile创建前注册自定义scheme
QWebEngineUrlScheme scheme("weread-local");
scheme.setSyntax(QWebEngineUrlScheme::Syntax::Path);
scheme.setDefaultPort(QWebEngineUrlScheme::PortUnspecified);
scheme.setFlags(QWebEngineUrlScheme::SecureScheme |
                QWebEngineUrlScheme::CorsEnabled);
QWebEngineUrlScheme::registerScheme(scheme);

// 2. 实现scheme handler
class LocalResourceHandler : public QWebEngineUrlSchemeHandler {
public:
    void requestStarted(QWebEngineUrlRequestJob *request) override {
        QString path = request->requestUrl().path();
        QFile *file = new QFile("/home/root/weread/local-assets" + path, request);

        if (file->open(QIODevice::ReadOnly)) {
            QString mimeType;
            if (path.endsWith(".js")) mimeType = "application/javascript";
            else if (path.endsWith(".css")) mimeType = "text/css";
            else if (path.endsWith(".png")) mimeType = "image/png";
            else mimeType = "application/octet-stream";

            request->reply(mimeType.toUtf8(), file);
        } else {
            request->fail(QWebEngineUrlRequestJob::UrlNotFound);
            delete file;
        }
    }
};

// 3. 安装handler到profile
profile->installUrlSchemeHandler("weread-local", new LocalResourceHandler());

// 4. 在interceptRequest中重定向URL
void interceptRequest(QWebEngineUrlRequestInfo &info) override {
    QString url = info.requestUrl().toString();

    // app.js
    if (url.contains("/wrwebnjlogic/js/app.") && url.endsWith(".js")) {
        info.redirect(QUrl("weread-local:///js/app.js"));
        return;
    }

    // common.js
    if (url.contains("/wrwebnjlogic/js/common.") && url.endsWith(".js")) {
        info.redirect(QUrl("weread-local:///js/common.js"));
        return;
    }

    // utils.js
    if (url.contains("/wrwebnjlogic/js/utils.") && url.endsWith(".js")) {
        info.redirect(QUrl("weread-local:///js/utils.js"));
        return;
    }

    // app.css
    if (url.contains("/wrwebnjlogic/css/app.") && url.endsWith(".css")) {
        info.redirect(QUrl("weread-local:///css/app.css"));
        return;
    }
}
```

#### 优势
- ✅ 保持HTTPS origin安全性
- ✅ CORS兼容（scheme注册时设置了CorsEnabled标志）
- ✅ 快速本地文件访问
- ✅ 无安全违规
- ✅ **预期性能**: 79秒 → 15-20秒（基于失败测试的16.3秒）

#### 劣势
- 需要在应用启动早期注册scheme（在QWebEngineProfile创建前）
- 需要实现自定义handler类

---

### 方案2: Qt资源系统 (qrc://)

#### 实现步骤

**1. 创建资源文件: `weread-test/resources.qrc`**
```xml
<RCC>
  <qresource prefix="/weread">
    <file alias="js/app.js">../local-assets/js/app.js</file>
    <file alias="js/common.js">../local-assets/js/common.js</file>
    <file alias="js/utils.js">../local-assets/js/utils.js</file>
    <file alias="css/app.css">../local-assets/css/app.css</file>
  </qresource>
</RCC>
```

**2. 修改 `CMakeLists.txt`**
```cmake
qt_add_executable(WereadEinkBrowser
    app/main.cpp
    app/shm_writer.cpp
    app/drm_refresher.cpp
    resources.qrc  # 添加资源文件
)
```

**3. 注册qrc scheme handler**
```cpp
// Qt内置支持，需要注册handler
class QrcResourceHandler : public QWebEngineUrlSchemeHandler {
public:
    void requestStarted(QWebEngineUrlRequestJob *request) override {
        QString path = request->requestUrl().toString();
        path.replace("qrc://", ":");

        QFile *file = new QFile(path, request);
        if (file->open(QIODevice::ReadOnly)) {
            QString mimeType;
            if (path.endsWith(".js")) mimeType = "application/javascript";
            else if (path.endsWith(".css")) mimeType = "text/css";
            request->reply(mimeType.toUtf8(), file);
        } else {
            request->fail(QWebEngineUrlRequestJob::UrlNotFound);
            delete file;
        }
    }
};

profile->installUrlSchemeHandler("qrc", new QrcResourceHandler());
```

**4. 重定向URL**
```cpp
void interceptRequest(QWebEngineUrlRequestInfo &info) override {
    QString url = info.requestUrl().toString();
    if (url.contains("/wrwebnjlogic/js/app.") && url.endsWith(".js")) {
        info.redirect(QUrl("qrc:///weread/js/app.js"));
    }
}
```

#### 优势
- ✅ Qt内置支持，稳定可靠
- ✅ 资源编译进二进制（比磁盘读取更快）
- ✅ 无文件系统依赖

#### 劣势
- ❌ **二进制体积显著增加** (~8MB for JS/CSS files)
- ❌ **更新资源需要重新编译** (不灵活)
- ❌ 目前二进制710KB，加入资源后变为8.7MB

---

### 方案3: 本地HTTP服务器

#### 实现代码
```cpp
// 使用QtHttpServer或轻量级HTTP库
// 在localhost:8080提供本地文件服务

#include <QtHttpServer>

QHttpServer *server = new QHttpServer();
server->route("/js/<arg>", [](const QString &file) {
    QFile f("/home/root/weread/local-assets/js/" + file);
    if (f.open(QIODevice::ReadOnly)) {
        return QHttpServerResponse("application/javascript", f.readAll());
    }
    return QHttpServerResponse(QHttpServerResponse::StatusCode::NotFound);
});
server->listen(QHostAddress::LocalHost, 8080);

// 重定向到本地服务器
void interceptRequest(QWebEngineUrlRequestInfo &info) override {
    QString url = info.requestUrl().toString();
    if (url.contains("cdn.weread.qq.com")) {
        QString path = info.requestUrl().path();
        info.redirect(QUrl("http://localhost:8080" + path));
    }
}
```

#### 优势
- ✅ 标准HTTP协议，无CORS问题
- ✅ 灵活，易于更新文件
- ✅ 可以服务多个进程

#### 劣势
- ❌ 需要运行HTTP服务器进程（增加复杂度）
- ❌ HTTP协议有轻微开销（vs 直接文件访问）
- ❌ 需要额外端口管理

---

## 推荐实施方案

### 优先级排序

1. **方案1: 自定义URL Scheme Handler** - **强烈推荐**
   - 预期性能提升: 79秒 → 15-20秒（**~75%性能提升**）
   - 实现复杂度: 中等
   - 维护成本: 低
   - 优先级: **HIGH**

2. **方案2: Qt资源系统 (qrc://)** - **备选方案**
   - 预期性能提升: 类似方案1
   - 缺点: 二进制增大8MB，更新需重编译
   - 优先级: **MEDIUM**（资源不常变化时可用）

3. **方案3: 本地HTTP服务器** - **最后选择**
   - 预期性能提升: 略低于方案1
   - 缺点: 复杂度高，需要额外进程
   - 优先级: **LOW**

---

## 实施步骤（方案1）

### Phase 1: 准备本地资源（已完成）
```bash
# 已下载核心资源到设备
/home/root/weread/local-assets/
├── js/
│   ├── app.js (4.9MB)
│   ├── common.js (1.1MB)
│   └── utils.js (1.9MB)
└── css/
    ├── app.css (213KB)
    └── 0.css (4.1KB)
```

### Phase 2: 实现自定义URL Scheme Handler
1. 在 `main.cpp` 开头添加scheme注册代码
2. 实现 `LocalResourceHandler` 类
3. 在QWebEngineProfile创建后安装handler
4. 修改 `ResourceInterceptor::interceptRequest()` 添加重定向逻辑

### Phase 3: 编译测试
```bash
docker exec qt6-arm-builder bash -c "
  cd /workspace/weread-test && \
  rm -rf build-cross && mkdir build-cross && \
  /workspace/qt6-src/build-qt6/qtbase/bin/qt-cmake -G Ninja \
    -S /workspace/weread-test -B /workspace/weread-test/build-cross \
    -DCMAKE_C_COMPILER=aarch64-linux-gnu-gcc \
    -DCMAKE_CXX_COMPILER=aarch64-linux-gnu-g++ \
    -DCMAKE_PREFIX_PATH=/workspace/qt6-src/build-qt6/qtbase/lib/cmake && \
  cmake --build /workspace/weread-test/build-cross
"
```

### Phase 4: 部署验证
```bash
# 部署到设备
sshpass -p 'QpEXvfq2So' scp /Users/jameszhu/AI_Projects/remarkableweread/weread-test/build-cross/WereadEinkBrowser root@10.11.99.1:/tmp/
sshpass -p 'QpEXvfq2So' ssh root@10.11.99.1 "
  cd /home/root/weread && \
  pkill -9 WereadEinkBrowser epaper_shm_viewer && \
  mv /tmp/WereadEinkBrowser WereadEinkBrowser && \
  chmod +x WereadEinkBrowser && \
  ./start-weread.sh
"

# 检查日志验证性能
sshpass -p 'QpEXvfq2So' ssh root@10.11.99.1 "tail -100 /home/root/weread/logs/weread-offscreen.log"
```

### Phase 5: 性能验证
- 检查 `loadFinished` 时间戳（预期15-20秒）
- 检查屏幕白度（应降至0-30%，内容正常显示）
- 验证点击翻页功能
- 验证书籍同步功能

---

## 风险与缓解措施

### 风险1: Scheme注册时机问题
**描述**: `QWebEngineUrlScheme::registerScheme()` 必须在 `QWebEngineProfile` 创建前调用

**缓解**:
```cpp
int main(int argc, char *argv[]) {
    // 1. 先注册scheme（在任何Qt WebEngine对象创建前）
    QWebEngineUrlScheme scheme("weread-local");
    scheme.setFlags(QWebEngineUrlScheme::SecureScheme |
                    QWebEngineUrlScheme::CorsEnabled);
    QWebEngineUrlScheme::registerScheme(scheme);

    // 2. 然后创建QApplication
    QApplication app(argc, argv);

    // 3. 最后创建QWebEngineProfile
    QWebEngineProfile *profile = new QWebEngineProfile();
}
```

### 风险2: MIME类型不匹配
**描述**: 错误的MIME类型可能导致资源加载失败

**缓解**: 精确匹配MIME类型
```cpp
QString getMimeType(const QString &path) {
    if (path.endsWith(".js")) return "application/javascript";
    if (path.endsWith(".css")) return "text/css";
    if (path.endsWith(".png")) return "image/png";
    if (path.endsWith(".jpg") || path.endsWith(".jpeg")) return "image/jpeg";
    if (path.endsWith(".woff2")) return "font/woff2";
    return "application/octet-stream";
}
```

### 风险3: 本地文件缺失
**描述**: 如果本地资源文件不存在，回退到网络加载

**缓解**: 实现fallback机制
```cpp
void interceptRequest(QWebEngineUrlRequestInfo &info) override {
    QString localPath = mapToLocalPath(info.requestUrl());
    if (QFile::exists(localPath)) {
        info.redirect(QUrl("weread-local://" + localPath));
    }
    // 否则允许网络加载（不做任何处理）
}
```

---

## 后续优化方向

1. **JavaScript执行时间分析**
   - 当前瓶颈解决后，可能转移到JS执行
   - 使用Chrome DevTools远程调试分析
   - 考虑禁用非必要JS模块

2. **增量本地化**
   - 先优化核心3个JS文件（app.js, common.js, utils.js）
   - 观察性能提升后，决定是否本地化更多资源
   - 平衡存储空间和性能收益

3. **存储优化**
   - 将本地资源存储在更快的存储介质
   - 考虑使用tmpfs (内存文件系统) 存储关键资源

---

## 参考资料

- **Qt官方文档**: [QWebEngineUrlSchemeHandler](https://doc.qt.io/qt-6/qwebengineurlschemehandler.html)
- **Qt官方文档**: [QWebEngineUrlScheme](https://doc.qt.io/qt-6/qwebengineurlscheme.html)
- **Qt官方示例**: [WebEngine Custom Scheme](https://doc.qt.io/qt-6/qtwebengine-webenginewidgets-customscheme-example.html)
- **CORS规范**: [MDN - CORS](https://developer.mozilla.org/en-US/docs/Web/HTTP/CORS)
- **Browser Performance API**: [MDN - Performance API](https://developer.mozilla.org/en-US/docs/Web/API/Performance_API)

---

**最后更新**: 2025-12-06
**测试设备**: reMarkable Paper Pro (ARM64)
**测试版本**: WereadEinkBrowser 710KB (缓存优化版本)
