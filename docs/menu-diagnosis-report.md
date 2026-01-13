# 微信读书菜单命令诊断报告

## 问题概述

用户报告下拉菜单上的4个命令（目录、字体+、字体-、主题）没有按照预期实现。

## 日志分析结果

### 1. 目录功能（Catalog）- **失败**

**日志输出**（连续5次尝试）：
```
[MENU] catalog click QVariant(QVariantMap, QMap(
  ("clicked", QVariant(bool, false))
  ("panelTag", QVariant(QString, "readerCatalog"))
  ("panelVisible", QVariant(bool, false))
))
```

**分析**：
- `clicked: false` - CSS选择器未找到按钮，点击失败
- `panelTag: "readerCatalog"` - 目录面板DOM元素**确实存在**，class名为`readerCatalog`
- `panelVisible: false` - 但面板处于隐藏状态

**根本原因**：
```javascript
// main.cpp:236 中的选择器
const btn=document.querySelector('.readerControls .catalog');
```
这个选择器**无法匹配**当前微信读书页面的实际DOM结构。

**证据**：
1. 选择器尝试查找 `.readerControls .catalog`（一个在`.readerControls`容器内的`.catalog`元素）
2. 但click操作返回`false`，说明`btn`为`null`
3. 然而后续的面板查找**找到了**`readerCatalog`元素，说明微信读书确实有目录功能
4. **推断**：目录按钮的实际class名不是`.catalog`，或者不在`.readerControls`容器内

**需要获取的信息**：
- 目录按钮的真实CSS选择器是什么？
- 可能的变体：`.reader-catalog`, `.catalogBtn`, `.wr_catalog_btn` 等

---

### 2. 字体调整功能（Font +/-）- **部分失败**

**日志输出**（字体+，第一次）：
```
[MENU] font step result QVariant(QVariantMap, QMap(
  ("active", QVariant(int, 1))         # 当前激活点：第1个
  ("target", QVariant(int, 2))         # 目标点：第2个
  ("after", QVariant(int, 1))          # 操作后激活点：仍然是第1个 ❌
  ("beforePx", QVariant(int, 28))      # 操作前字体：28px
  ("afterPx", QVariant(int, 30))       # 操作后字体：30px ✓
  ("fallback", QVariant(bool, true))   # 使用了降级方案
  ("reason", QVariant(QString, "unchanged"))  # 滑块状态未改变
  ("panelVisible", QVariant(bool, true))
  ("total", QVariant(int, 7))          # 总共7个字体档位
))
```

**日志输出**（字体+，第二次）：
```
[MENU] font step result QVariant(QVariantMap, QMap(
  ("active", QVariant(int, 1))
  ("target", QVariant(int, 2))
  ("after", QVariant(int, 1))          # 仍然未改变
  ("beforePx", QVariant(int, 30))      # 从30px开始
  ("afterPx", QVariant(int, 30))       # 仍然是30px ❌
  ("fallback", QVariant(bool, true))
  ("reason", QVariant(QString, "unchanged"))
))
```

**分析**：

**DOM操作阶段失败**：
```javascript
// main.cpp:257-276 尝试的操作
active = document.querySelector('.readerFooterBar_font_dot.active');  # 找到第1个点
target = dots[2];  # 第2个点（索引从0开始）
target.click();    # 尝试点击
// 但点击后，滑块状态没有改变（after仍然是1）
```

**CSS降级方案部分成功**：
- 第一次：`28px → 30px` ✓ 有效果
- 第二次：`30px → 30px` ✗ 无效果

**根本原因**：
1. **点击事件被拦截或无效**：滑块上的点虽然能被选中，但点击后不触发微信读书的字体改变逻辑
2. **CSS注入方法有限制**：
   - 第一次有效是因为初始字体是28px，被强制改为30px
   - 第二次无效是因为已经是30px，注入相同的值不会触发微信读书的重新渲染
   - **关键问题**：CSS注入只能设置固定的像素值（如30px），无法实现"增大一档"的相对调整

**需要获取的信息**：
- 微信读书字体滑块的7个档位分别对应多少px？
- 是否有API可以直接调用（如 `window.setFontSize(level)` 或类似方法）？
- 点击事件为何无效？是否需要触发其他事件（如`mousedown`、`mouseup`）？

---

### 3. 主题切换功能（Theme）- **成功** ✓

**日志输出**：
```
[MENU] theme toggle QVariant(QVariantMap, QMap(
  ("ok", QVariant(bool, true))
  ("before", QVariant(QString, "unknown"))
  ("beforeAttr", QVariant(QString, ""))
  ("after", QVariant(QString, "dark"))
  ("afterAttr", QVariant(QString, "dark"))
  ("btn", QVariant(QString, "white"))
  ("bodyCls", QVariant(QString, "wr_page_reader wr_reader_font_size_level_1"))
  ("htmlCls", QVariant(QString, ""))
))
```

**分析**：
- `ok: true` ✓ 操作成功
- `before: "unknown"` - 初始状态未知（因为`<html>`元素没有`data-theme`属性）
- `after: "dark"` - 成功切换到暗色模式
- `btn: "white"` - 找到了"白色"按钮并点击

**工作机制**（main.cpp:307-343）：
```javascript
const themeBtn = Array.from(document.querySelectorAll('.readerFooterBar_theme_btn'))
                      .find(el => el.textContent.includes(targetText));
themeBtn.click();
```

**为什么成功**：
- 选择器 `.readerFooterBar_theme_btn` 正确
- 通过文本内容（"白色"/"深色"）查找按钮，避免了依赖特定class
- 点击事件有效触发了主题切换

---

## 初步诊断结论

### 菜单系统本身正常运行
- ✓ 菜单可以显示（`[MENU] show overlay`）
- ✓ 触摸事件正确接收（类型107-111）
- ✓ JavaScript注入机制有效
- ✓ 菜单按钮可以点击（非`WA_TransparentForMouseEvents`问题）

### 具体问题

| 功能 | 状态 | 根本原因 | 影响程度 |
|------|------|----------|----------|
| 主题切换 | ✓ 正常 | 选择器正确，事件有效 | 无问题 |
| 目录 | ✗ 失败 | CSS选择器`.readerControls .catalog`不匹配实际DOM | 完全无法使用 |
| 字体调整 | △ 部分失败 | 1) 点击滑块无效<br>2) CSS降级只能设置固定值，无法相对调整 | 功能受限，无法连续调整 |

---

## 需要采取的行动

### 立即需要获取的信息

#### 1. 目录按钮的真实选择器
创建一个JavaScript诊断脚本，遍历可能的选择器：
```javascript
// 候选选择器
[
  '.catalog',
  '.reader-catalog',
  '.catalogBtn',
  '.wr_catalog',
  '.readerCatalog_btn',
  // ... 更多可能性
].forEach(sel => {
  const el = document.querySelector(sel);
  if (el) console.log('Found:', sel, el);
});

// 或者查找所有包含"目录"文本的按钮
Array.from(document.querySelectorAll('button, div[role="button"], .btn'))
  .filter(el => el.textContent.includes('目录'))
  .forEach(el => console.log('Catalog candidate:', el.className, el));
```

#### 2. 字体调整的正确方法
```javascript
// 检查是否有全局API
console.log('Font API:', window.setFontSize, window.wrFontSize);

// 获取字体档位配置
const dots = document.querySelectorAll('.readerFooterBar_font_dot');
dots.forEach((dot, i) => {
  console.log(`Level ${i}:`, dot.dataset, getComputedStyle(dot));
});

// 测试事件触发
const dot = dots[2];
['click', 'mousedown', 'mouseup', 'touchstart', 'touchend'].forEach(evt => {
  dot.dispatchEvent(new Event(evt, {bubbles: true}));
});
```

### 修复方案建议（待确认信息后实施）

#### 方案A：更新CSS选择器（目录）
如果找到正确的选择器（例如 `.reader-catalog-btn`），修改main.cpp:236：
```cpp
const btn=document.querySelector('.reader-catalog-btn');  // 使用正确的选择器
```

#### 方案B：改用文本匹配（目录）
参考主题切换的成功经验，使用文本内容查找：
```javascript
const btn = Array.from(document.querySelectorAll('button, div[role="button"]'))
              .find(el => el.textContent.includes('目录') || el.textContent.includes('Catalog'));
```

#### 方案C：调用微信读书API（字体）
如果发现全局API（如`window.__WEREAD__.setFontSize(level)`），直接调用而不操作DOM：
```javascript
if (window.__WEREAD__ && window.__WEREAD__.setFontSize) {
  const currentLevel = getCurrentFontLevel();
  window.__WEREAD__.setFontSize(currentLevel + (increase ? 1 : -1));
}
```

#### 方案D：改进CSS降级方案（字体）
预定义所有7个字体档位的像素值，根据当前档位计算下一档：
```javascript
const fontSizes = [20, 24, 28, 32, 36, 40, 44];  // 假设的7档
const currentLevel = getCurrentLevelFromPx(currentPx);
const newLevel = Math.min(6, Math.max(0, currentLevel + (increase ? 1 : -1)));
document.body.style.fontSize = fontSizes[newLevel] + 'px';
```

---

## 下一步操作建议

1. **获取页面DOM信息**：
   - 修改main.cpp临时添加DOM探测JavaScript
   - 或通过浏览器开发者工具连接到WebEngine
   - 或在设备上运行诊断脚本

2. **验证修复方案**：
   - 在本地测试环境验证新选择器
   - 确认字体API的存在和用法

3. **实施修复**：
   - 更新main.cpp中的JavaScript代码
   - 重新编译并部署到设备
   - 测试所有菜单功能

---

## 附录：相关代码位置

- **目录功能**：[main.cpp:232-246](src/app/main.cpp#L232-L246)
- **字体调整**：[main.cpp:247-305](src/app/main.cpp#L247-L305)
- **主题切换**：[main.cpp:307-343](src/app/main.cpp#L307-L343)
- **触摸事件处理**：[main.cpp:445-480](src/app/main.cpp#L445-L480)
- **前端日志**：`/home/root/weread/logs/weread-offscreen.log`
