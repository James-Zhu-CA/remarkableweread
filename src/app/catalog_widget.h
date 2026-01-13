#ifndef CATALOG_WIDGET_H
#define CATALOG_WIDGET_H

#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

class CatalogWidget : public QWidget {
  Q_OBJECT

public:
  explicit CatalogWidget(QWidget *parent = nullptr);
  void loadChapters(const QJsonArray &chapters);

signals:
  void chapterClicked(int index, const QString &chapterUid);
  void closed();
  void requestRefresh(); // Request E-ink hardware refresh

protected:
  void showEvent(QShowEvent *event) override;
  void paintEvent(QPaintEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
  void onItemClicked(QListWidgetItem *item);
  void onPrevPage();
  void onNextPage();
  void onClose();

private:
  void updatePagination();
  void setupUi();

  QListWidget *m_listWidget;
  QPushButton *m_closeBtn;
  QPushButton *m_prevBtn;
  QPushButton *m_nextBtn;
  QLabel *m_titleLabel;
  QLabel *m_pageLabel;

  int m_currentPage = 0;
  int m_itemsPerPage = 8; // Adjust based on e-ink screen size
  QJsonArray m_chapters;
};

#endif // CATALOG_WIDGET_H
