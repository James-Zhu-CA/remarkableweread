#include "catalog_widget.h"

#include <QDebug>
#include <QJsonObject>

#include <QPainter>

#include <QTimer>
#include <QTouchEvent>

CatalogWidget::CatalogWidget(QWidget *parent) : QWidget(parent) {
  setupUi();
  // Hide by default
  hide();

  // Set object name for styling if needed
  setObjectName("CatalogWidget");

  // Critical: Accept touch events and grab focus to prevent pass-through
  setAttribute(Qt::WA_AcceptTouchEvents, true);
  setFocusPolicy(Qt::StrongFocus);
  // Ensure this widget captures all mouse/touch events in its area
  setMouseTracking(true);

  // E-ink style: white background, black text (doubled font sizes for
  // readability)
  // Set autoFillBackground to ensure the background is painted
  setAutoFillBackground(true);
  QPalette pal = palette();
  pal.setColor(QPalette::Window, Qt::white);
  setPalette(pal);

  setStyleSheet(R"(
        QWidget#CatalogWidget {
            background-color: white;
            border: 4px solid black;
        }
        QLabel {
            font-size: 48px;
            font-weight: bold;
            color: black;
            background-color: white;
        }
        QListWidget {
            font-size: 40px;
            border: none;
            outline: none;
            background-color: white;
            color: black;
        }
        QListWidget::item {
            padding: 30px;
            border-bottom: 2px solid #ccc;
            background-color: white;
        }
        QListWidget::item:selected {
            background-color: black;
            color: white;
        }
        QPushButton#closeBtn, QPushButton#pageBtn {
            font-size: 40px;
            border: none;
            padding: 20px;
            min-height: 80px;
            background-color: white;
            color: black;
        }
        QPushButton#closeBtn:pressed, QPushButton#pageBtn:pressed {
            background-color: #ddd;
            color: black;
        }
        QScrollBar:vertical {
            background-color: white;
            width: 20px;
        }
        QScrollBar::handle:vertical {
            background-color: #888;
        }
    )");
}

void CatalogWidget::setupUi() {
  QVBoxLayout *mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(30, 20, 30, 20);
  mainLayout->setSpacing(0); // No gaps between elements

  // Header row (same white background as parent)
  QHBoxLayout *headerLayout = new QHBoxLayout();
  headerLayout->setContentsMargins(0, 10, 0, 15);

  m_titleLabel = new QLabel("目录 / Contents", this);
  m_closeBtn = new QPushButton("关闭 / Close", this);
  m_closeBtn->setObjectName("closeBtn");
  m_closeBtn->setCursor(Qt::PointingHandCursor);
  m_closeBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  connect(m_closeBtn, &QPushButton::clicked, this, &CatalogWidget::onClose);

  headerLayout->addWidget(m_titleLabel);
  headerLayout->addStretch();
  headerLayout->addWidget(m_closeBtn);
  mainLayout->addLayout(headerLayout);

  // List - ensure touch events work
  m_listWidget = new QListWidget(this);
  m_listWidget->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  m_listWidget->setSelectionMode(QAbstractItemView::SingleSelection);
  // Enable touch events on the list widget
  m_listWidget->setAttribute(Qt::WA_AcceptTouchEvents, true);
  m_listWidget->viewport()->setAttribute(Qt::WA_AcceptTouchEvents, true);
  // Install event filter to convert touch to click
  m_listWidget->viewport()->installEventFilter(this);
  // Connect both clicked and pressed for better touch screen compatibility
  connect(m_listWidget, &QListWidget::itemClicked, this,
          &CatalogWidget::onItemClicked);
  connect(m_listWidget, &QListWidget::itemPressed, this,
          [this](QListWidgetItem *item) {
            qInfo() << "[CATALOG] itemPressed signal received";
          });
  // Also connect itemActivated for keyboard/enter events
  connect(m_listWidget, &QListWidget::itemActivated, this,
          [this](QListWidgetItem *item) {
            qInfo() << "[CATALOG] itemActivated signal received";
            onItemClicked(item);
          });
  mainLayout->addWidget(m_listWidget);

  // Footer row (same white background as parent)
  QHBoxLayout *footerLayout = new QHBoxLayout();
  footerLayout->setContentsMargins(0, 15, 0, 10);

  m_prevBtn = new QPushButton("上一页 / Prev", this);
  m_prevBtn->setObjectName("pageBtn");
  m_nextBtn = new QPushButton("下一页 / Next", this);
  m_nextBtn->setObjectName("pageBtn");
  m_pageLabel = new QLabel("0 / 0", this);
  m_pageLabel->setAlignment(Qt::AlignCenter);

  connect(m_prevBtn, &QPushButton::clicked, this, &CatalogWidget::onPrevPage);
  connect(m_nextBtn, &QPushButton::clicked, this, &CatalogWidget::onNextPage);

  footerLayout->addWidget(m_prevBtn);
  footerLayout->addStretch();
  footerLayout->addWidget(m_pageLabel);
  footerLayout->addStretch();
  footerLayout->addWidget(m_nextBtn);
  mainLayout->addLayout(footerLayout);
}

void CatalogWidget::loadChapters(const QJsonArray &chapters) {
  m_chapters = chapters;
  m_currentPage = 0;

  // Find current chapter index to set initial page
  for (int i = 0; i < m_chapters.size(); ++i) {
    QJsonObject ch = m_chapters[i].toObject();
    if (ch["isCurrent"].toBool()) {
      m_currentPage = i / m_itemsPerPage;
      break;
    }
  }

  updatePagination();
  show();
  raise(); // Ensure on top
}

void CatalogWidget::updatePagination() {
  m_listWidget->clear();

  int start = m_currentPage * m_itemsPerPage;
  int end = qMin(start + m_itemsPerPage, m_chapters.size());

  for (int i = start; i < end; ++i) {
    QJsonObject ch = m_chapters[i].toObject();
    QString title = ch["title"].toString();
    int level = ch["level"].toInt(1);
    const QString matchKey = ch["matchKey"].toString();
    const QString key = matchKey.isEmpty() ? ch["chapterUid"].toString() : matchKey;

    // Indent based on level (optional, simple spacing)
    QString indent = "";
    if (level > 1)
      indent = QString("  ").repeated(level - 1);

    QListWidgetItem *item = new QListWidgetItem(indent + title);

    // Store the original index in user data
    item->setData(Qt::UserRole, ch["index"].toInt());
    item->setData(Qt::UserRole + 1, key);

    if (ch["isCurrent"].toBool()) {
      QFont f = item->font();
      f.setBold(true);
      item->setFont(f);
      item->setText("➤ " + item->text());
      item->setSelected(true);
    }

    m_listWidget->addItem(item);
  }

  // Update buttons
  m_prevBtn->setEnabled(m_currentPage > 0);
  int totalPages = (m_chapters.size() + m_itemsPerPage - 1) / m_itemsPerPage;
  m_nextBtn->setEnabled(m_currentPage < totalPages - 1);

  m_pageLabel->setText(
      QString("%1 / %2").arg(m_currentPage + 1).arg(totalPages));
}

void CatalogWidget::onItemClicked(QListWidgetItem *item) {
  qInfo() << "[CATALOG] onItemClicked called";
  if (!item)
    return;
  int index = item->data(Qt::UserRole).toInt();
  QString key = item->data(Qt::UserRole + 1).toString();
  qInfo() << "[CATALOG] emitting chapterClicked for index" << index << "key"
          << key;
  emit chapterClicked(index, key);
}

void CatalogWidget::onPrevPage() {
  qInfo() << "[CATALOG] onPrevPage called, currentPage =" << m_currentPage;
  if (m_currentPage > 0) {
    m_currentPage--;
    updatePagination();
    // Request E-ink hardware refresh with delays
    QTimer::singleShot(100, this, [this]() { emit requestRefresh(); });
    QTimer::singleShot(500, this, [this]() { emit requestRefresh(); });
    QTimer::singleShot(1000, this, [this]() { emit requestRefresh(); });
  }
}

void CatalogWidget::onNextPage() {
  qInfo() << "[CATALOG] onNextPage called, currentPage =" << m_currentPage;
  int totalPages = (m_chapters.size() + m_itemsPerPage - 1) / m_itemsPerPage;
  if (m_currentPage < totalPages - 1) {
    m_currentPage++;
    updatePagination();
    // Request E-ink hardware refresh with delays
    QTimer::singleShot(100, this, [this]() { emit requestRefresh(); });
    QTimer::singleShot(500, this, [this]() { emit requestRefresh(); });
    QTimer::singleShot(1000, this, [this]() { emit requestRefresh(); });
  }
}

void CatalogWidget::onClose() {
  qInfo() << "[CATALOG] onClose called";
  hide();
  // Force parent widget to repaint (critical for E-ink displays)
  if (parentWidget()) {
    parentWidget()->update();
    parentWidget()->repaint();
  }
  emit closed();
  // Request hardware refresh
  emit requestRefresh();
}

void CatalogWidget::showEvent(QShowEvent *event) {
  QWidget::showEvent(event);
  // Adjust geometry to center or cover screen if parent exists
  if (parentWidget()) {
    resize(parentWidget()->width() * 0.9, parentWidget()->height() * 0.9);
    move(parentWidget()->width() * 0.05, parentWidget()->height() * 0.05);
  }
  // Grab focus when shown to capture events
  setFocus();
  raise();

  // Request hardware refresh with delays for E-ink
  QTimer::singleShot(100, this, [this]() { emit requestRefresh(); });
  QTimer::singleShot(500, this, [this]() { emit requestRefresh(); });
}

// Custom paint event to ensure background is solid white (fixing gaps)
void CatalogWidget::paintEvent(QPaintEvent *event) {
  QPainter painter(this);
  painter.fillRect(rect(), Qt::white);
}

void CatalogWidget::mousePressEvent(QMouseEvent *event) {
  qInfo() << "[CATALOG] mousePressEvent at" << event->pos();
  // Accept the event so it doesn't propagate to underlying widgets
  event->accept();
  QWidget::mousePressEvent(event);
}

bool CatalogWidget::eventFilter(QObject *watched, QEvent *event) {
  // Convert touch events on QListWidget viewport to mouse clicks
  if (watched == m_listWidget->viewport()) {
    if (event->type() == QEvent::TouchEnd) {
      auto *touchEvent = static_cast<QTouchEvent *>(event);
      if (!touchEvent->points().isEmpty()) {
        QPointF pos = touchEvent->points().first().position();
        qInfo() << "[CATALOG] TouchEnd on viewport at" << pos;

        // Find item at touch position
        QListWidgetItem *item = m_listWidget->itemAt(pos.toPoint());
        if (item) {
          qInfo() << "[CATALOG] Touch hit item:" << item->text();
          // Directly call the click handler
          onItemClicked(item);
          event->accept();
          return true;
        }
      }
    } else if (event->type() == QEvent::TouchBegin) {
      // Accept touch begin to receive touch end
      event->accept();
      return true;
    }
  }
  return QWidget::eventFilter(watched, event);
}
