# 如何使用DOM诊断工具

## 目的

获取微信读书页面的实际DOM结构，找出正确的CSS选择器来修复菜单功能。

## 方法一：临时修改main.cpp注入诊断脚本（推荐）

### 步骤

1. **在main.cpp中添加诊断代码**

   在 `pageLoaded()` 函数中（约第215行），添加诊断JavaScript注入：

   ```cpp
   void pageLoaded() {
       qInfo() << "[WEREAD] Page loaded";

       // === 添加这段诊断代码 ===
       const QString diagnosticJS = QStringLiteral(R"(
           (function() {
               console.log('=== DOM Diagnostic ===');

               // 目录按钮候选选择器
               const catalogSelectors = [
                   '.readerControls .catalog',
                   '.catalog', '.reader-catalog', '.catalogBtn',
                   '.wr_catalog', '.readerCatalog_btn'
               ];

               const catalogFound = [];
               catalogSelectors.forEach(sel => {
                   const el = document.querySelector(sel);
                   if (el) catalogFound.push({
                       sel: sel,
                       tag: el.tagName,
                       cls: el.className,
                       txt: el.textContent.substring(0, 30)
                   });
               });

               // 字体滑块信息
               const fontDots = document.querySelectorAll('.readerFooterBar_font_dot');
               const fontInfo = {
                   count: fontDots.length,
                   levels: Array.from(fontDots).map((dot, i) => ({
                       idx: i,
                       active: dot.classList.contains('active'),
                       data: dot.dataset
                   }))
               };

               // 返回结果
               return {
                   catalog: catalogFound,
                   font: fontInfo,
                   body: document.body.className
               };
           })()
       )");

       if (m_view && m_view->page()) {
           m_view->page()->runJavaScript(diagnosticJS, [](const QVariant &result) {
               qInfo() << "[DIAGNOSTIC] DOM Info:" << result;
           });
       }
       // === 诊断代码结束 ===
   }
   ```

2. **重新编译**

   ```bash
   cd /Users/jameszhu/AI_Projects/remarkableweread/src
   docker exec -it qt6-arm-builder bash
   cd /workspace/build-weread
   make -j4
   ```

3. **部署到设备**

   ```bash
   sshpass -p 'QpEXvfq2So' scp build-weread/src/app/WereadEinkBrowser root@10.11.99.1:/home/root/weread/
   ```

4. **启动WeRead并查看日志**

   ```bash
   # 通过魔法书启动，或手动启动：
   sshpass -p 'QpEXvfq2So' ssh root@10.11.99.1 'cd /home/root/weread && ./start-weread.sh'

   # 查看诊断输出：
   sshpass -p 'QpEXvfq2So' ssh root@10.11.99.1 'tail -100 /home/root/weread/logs/weread-offscreen.log | grep DIAGNOSTIC'
   ```

5. **分析结果**

   日志中会显示类似：
   ```
   [DIAGNOSTIC] DOM Info: QVariant(QVariantMap, QMap(
     ("catalog", QVariant(QVariantList, [...]))
     ("font", QVariant(QVariantMap, {...}))
   ))
   ```

---

## 方法二：使用完整诊断脚本（如果需要更详细信息）

### 步骤

1. **修改main.cpp读取外部诊断脚本**

   ```cpp
   void pageLoaded() {
       qInfo() << "[WEREAD] Page loaded";

       // 读取诊断脚本文件
       QFile diagFile("/home/root/weread/weread-dom-diagnostic.js");
       if (diagFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
           QString diagnosticJS = QTextStream(&diagFile).readAll();
           diagFile.close();

           if (m_view && m_view->page()) {
               m_view->page()->runJavaScript(diagnosticJS, [](const QVariant &result) {
                   qInfo() << "[DIAGNOSTIC] Full report:" << result;
               });
           }
       }
   }
   ```

2. **部署诊断脚本到设备**

   ```bash
   sshpass -p 'QpEXvfq2So' scp weread-dom-diagnostic.js root@10.11.99.1:/home/root/weread/
   ```

3. **重新编译并部署WereadEinkBrowser**（同方法一）

4. **查看完整诊断报告**

   ```bash
   sshpass -p 'QpEXvfq2So' ssh root@10.11.99.1 'tail -200 /home/root/weread/logs/weread-offscreen.log | grep -A 50 "DIAGNOSTIC"'
   ```

---

## 方法三：手动通过浏览器开发者工具（如果可能）

如果Qt WebEngine支持远程调试：

1. **启用WebEngine远程调试**

   在main.cpp的main函数中添加：
   ```cpp
   qputenv("QTWEBENGINE_REMOTE_DEBUGGING_PORT", "9222");
   ```

2. **从Mac连接**

   ```bash
   # 建立SSH隧道
   ssh -L 9222:localhost:9222 root@10.11.99.1

   # 在Chrome中打开
   # chrome://inspect
   ```

3. **在开发者工具控制台中运行诊断脚本**

   复制 `weread-dom-diagnostic.js` 的内容到控制台执行。

---

## 预期输出

成功运行后，应该能得到：

### 目录按钮信息
```javascript
{
  "catalog": {
    "candidateSelectors": [
      {
        "selector": ".reader-catalog-btn",  // 正确的选择器！
        "found": true,
        "className": "reader-catalog-btn icon-btn",
        "visible": true
      }
    ],
    "foundByText": [
      {
        "className": "catalog-button",
        "text": "目录"
      }
    ]
  }
}
```

### 字体滑块信息
```javascript
{
  "font": {
    "dots": {
      "count": 7,
      "levels": [
        {"index": 0, "active": false, "fontSize": "20"},
        {"index": 1, "active": true, "fontSize": "24"},
        {"index": 2, "active": false, "fontSize": "28"},
        // ...
      ]
    },
    "api": {
      "windowWeread": true,
      "wereadMethods": ["setFontSize", "getFontSize"]
    }
  }
}
```

---

## 下一步

根据诊断结果：

1. **更新main.cpp中的CSS选择器**
2. **如果发现API，改用API调用而不是DOM操作**
3. **重新编译、部署、测试**

---

## 注意事项

- 诊断代码只是临时添加，获取信息后应该移除
- 编译诊断版本时可以在不同的目录，避免覆盖正式版本
- 保存诊断日志以便分析
