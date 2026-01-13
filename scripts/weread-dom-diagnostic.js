/**
 * WeRead DOM Diagnostic Script
 * 用于获取微信读书页面的实际DOM结构，帮助修复菜单选择器问题
 *
 * 使用方法：将此脚本注入到WeRead页面中执行
 */

(function() {
    console.log('=== WeRead DOM Diagnostic Start ===');

    const report = {
        timestamp: new Date().toISOString(),
        url: window.location.href,
        findings: {}
    };

    // ========================================
    // 1. 诊断：目录按钮
    // ========================================
    console.log('\n[1] Diagnosing CATALOG button...');

    const catalogCandidates = [
        '.readerControls .catalog',        // 当前使用的选择器（失败）
        '.catalog',
        '.reader-catalog',
        '.catalogBtn',
        '.catalog-btn',
        '.wr_catalog',
        '.readerCatalog',
        '.readerCatalog_btn',
        '.reader_catalog_btn',
        '.readerControls_catalog',
        '.readerFooterBar_catalog',
        '[data-action="catalog"]',
        '[data-testid="catalog"]',
    ];

    const catalogResults = [];
    catalogCandidates.forEach(selector => {
        try {
            const el = document.querySelector(selector);
            if (el) {
                catalogResults.push({
                    selector,
                    found: true,
                    tagName: el.tagName,
                    className: el.className,
                    id: el.id,
                    text: el.textContent.substring(0, 50),
                    visible: getComputedStyle(el).display !== 'none',
                    clickable: el.onclick !== null || el.addEventListener !== undefined
                });
                console.log(`  ✓ Found: ${selector}`, el);
            }
        } catch (e) {
            // Invalid selector
        }
    });

    // 查找所有包含"目录"文本的元素
    const catalogByText = [];
    document.querySelectorAll('button, div[role="button"], a, span').forEach(el => {
        const text = el.textContent.trim();
        if (text.includes('目录') || text.toLowerCase().includes('catalog')) {
            catalogByText.push({
                tagName: el.tagName,
                className: el.className,
                id: el.id,
                text: text.substring(0, 50),
                visible: getComputedStyle(el).display !== 'none'
            });
        }
    });

    report.findings.catalog = {
        candidateSelectors: catalogResults,
        foundByText: catalogByText,
        currentPanel: {
            selector: '.readerCatalog',
            exists: !!document.querySelector('.readerCatalog'),
            visible: document.querySelector('.readerCatalog')
                ? getComputedStyle(document.querySelector('.readerCatalog')).display !== 'none'
                : false
        }
    };

    console.log('  Catalog results:', report.findings.catalog);

    // ========================================
    // 2. 诊断：字体调整滑块
    // ========================================
    console.log('\n[2] Diagnosing FONT slider...');

    const fontPanel = document.querySelector('.readerFooterBar_font, .reader-font-panel, .font-panel');
    const fontDots = document.querySelectorAll('.readerFooterBar_font_dot, .font-dot, [class*="font"][class*="dot"]');

    const fontInfo = {
        panel: {
            found: !!fontPanel,
            selector: fontPanel ? fontPanel.className : null,
            visible: fontPanel ? getComputedStyle(fontPanel).display !== 'none' : false
        },
        dots: {
            selector: '.readerFooterBar_font_dot',
            count: fontDots.length,
            levels: []
        },
        currentFontSize: {
            bodyPx: getComputedStyle(document.body).fontSize,
            readerPx: null
        }
    };

    // 获取每个字体档位的信息
    fontDots.forEach((dot, index) => {
        const isActive = dot.classList.contains('active');
        fontInfo.dots.levels.push({
            index,
            active: isActive,
            className: dot.className,
            dataset: Object.assign({}, dot.dataset),
            fontSize: dot.dataset.fontSize || dot.getAttribute('data-font-size') || null
        });
    });

    // 查找阅读器内容区域的字体大小
    const readerContent = document.querySelector('.wr_readerContent, .reader-content, #contentBody');
    if (readerContent) {
        fontInfo.currentFontSize.readerPx = getComputedStyle(readerContent).fontSize;
        fontInfo.currentFontSize.readerSelector = readerContent.className;
    }

    // 检查是否有全局字体API
    fontInfo.api = {
        windowWeread: typeof window.__WEREAD__ !== 'undefined',
        wereadMethods: window.__WEREAD__ ? Object.keys(window.__WEREAD__) : [],
        setFontSize: typeof window.setFontSize === 'function',
        wrFontSize: typeof window.wrFontSize !== 'undefined'
    };

    report.findings.font = fontInfo;
    console.log('  Font info:', fontInfo);

    // ========================================
    // 3. 诊断：主题按钮（参考正常工作的案例）
    // ========================================
    console.log('\n[3] Diagnosing THEME buttons...');

    const themeButtons = document.querySelectorAll('.readerFooterBar_theme_btn');
    const themeInfo = {
        selector: '.readerFooterBar_theme_btn',
        count: themeButtons.length,
        buttons: [],
        currentTheme: {
            htmlAttr: document.documentElement.getAttribute('data-theme') || '',
            bodyClass: document.body.className,
            htmlClass: document.documentElement.className
        }
    };

    themeButtons.forEach((btn, index) => {
        themeInfo.buttons.push({
            index,
            text: btn.textContent.trim(),
            className: btn.className,
            visible: getComputedStyle(btn).display !== 'none'
        });
    });

    report.findings.theme = themeInfo;
    console.log('  Theme info:', themeInfo);

    // ========================================
    // 4. 查找readerControls容器
    // ========================================
    console.log('\n[4] Diagnosing readerControls container...');

    const readerControls = document.querySelector('.readerControls');
    const controlsInfo = {
        found: !!readerControls,
        children: []
    };

    if (readerControls) {
        Array.from(readerControls.children).forEach(child => {
            controlsInfo.children.push({
                tagName: child.tagName,
                className: child.className,
                id: child.id,
                text: child.textContent.substring(0, 30)
            });
        });
    }

    report.findings.readerControls = controlsInfo;
    console.log('  ReaderControls:', controlsInfo);

    // ========================================
    // 5. 页面整体信息
    // ========================================
    console.log('\n[5] Page structure...');

    report.findings.pageStructure = {
        bodyClasses: document.body.className.split(' '),
        htmlClasses: document.documentElement.className.split(' '),
        mainContainers: []
    };

    // 查找主要容器
    const containers = document.querySelectorAll('[class*="reader"], [class*="Reader"], [id*="reader"], [id*="Reader"]');
    const seen = new Set();
    containers.forEach(el => {
        const key = el.tagName + '.' + el.className;
        if (!seen.has(key) && seen.size < 20) {
            seen.add(key);
            report.findings.pageStructure.mainContainers.push({
                tagName: el.tagName,
                className: el.className,
                id: el.id
            });
        }
    });

    console.log('  Page structure:', report.findings.pageStructure);

    // ========================================
    // 输出完整报告
    // ========================================
    console.log('\n=== WeRead DOM Diagnostic Report ===');
    console.log(JSON.stringify(report, null, 2));

    // 将报告保存到全局变量，便于复制
    window.__WEREAD_DIAGNOSTIC_REPORT__ = report;
    console.log('\n✓ Report saved to window.__WEREAD_DIAGNOSTIC_REPORT__');
    console.log('  Copy it with: JSON.stringify(window.__WEREAD_DIAGNOSTIC_REPORT__, null, 2)');

    // 返回精简建议
    const suggestions = {
        catalog: catalogResults.length > 0
            ? `Use selector: ${catalogResults[0].selector}`
            : catalogByText.length > 0
            ? `Use text search: ${catalogByText[0].className}`
            : 'No catalog button found',
        font: fontInfo.dots.count > 0
            ? `Found ${fontInfo.dots.count} font levels, active: ${fontInfo.dots.levels.find(l => l.active)?.index ?? 'unknown'}`
            : 'No font slider found',
        theme: themeInfo.count > 0
            ? `Theme buttons working correctly (${themeInfo.count} buttons found)`
            : 'No theme buttons found'
    };

    console.log('\n=== Quick Suggestions ===');
    console.log(suggestions);

    return report;
})();
