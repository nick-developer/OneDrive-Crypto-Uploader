#pragma once

#include <QMainWindow>
#include <QNetworkAccessManager>
#include <memory>

#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QTextEdit>
#include <QTreeView>
#include <QTableView>
#include <QStandardItemModel>
#include <QSortFilterProxyModel>
#include <QToolButton>

#include "../graph/GraphClient.h"

class AuthManager;
class QHBoxLayout;

class MainWindow : public QMainWindow {
  Q_OBJECT
public:
  explicit MainWindow(QWidget* parent = nullptr);
  ~MainWindow() override; // out-of-line to avoid incomplete type issues

private:
  void setupUi();
  void loadConfig();
  void log(const QString& msg);

  void onSignIn();
  void onEncryptUpload();
  void onDownloadDecrypt();

  // OneDrive browser helpers
  void initOneDriveBrowser();
  void refreshCurrentFolder();
  void openFolder(const QString& folderId, const QString& folderName);
  void populateChildren(const QString& parentId, const QVector<DriveItemInfo>& items);
  QStandardItem* ensureDummyChild(QStandardItem* folderItem);

  // Breadcrumbs
  struct Crumb { QString id; QString name; };
  void updateBreadcrumbBar();
  void navigateToCrumb(int index);
  void setBreadcrumbsFromTreeItem(QStandardItem* item);

  QNetworkAccessManager nam_;
  std::unique_ptr<AuthManager> auth_;
  std::unique_ptr<GraphClient> graph_;

  QWidget* central_ = nullptr;
  QPushButton* signInBtn_ = nullptr;
  QLabel* statusLbl_ = nullptr;

  QWidget* breadcrumbBar_ = nullptr;
  QHBoxLayout* breadcrumbLayout_ = nullptr;
  QLineEdit* searchEdit_ = nullptr;
  QPushButton* clearSearchBtn_ = nullptr;

  QTreeView* oneDriveTree_ = nullptr;
  QTableView* oneDriveTable_ = nullptr;
  QPushButton* refreshDriveBtn_ = nullptr;
  QPushButton* upBtn_ = nullptr;
  QLabel* currentFolderLbl_ = nullptr;

  QStandardItemModel* treeModel_ = nullptr;
  QStandardItemModel* tableModel_ = nullptr;
  QSortFilterProxyModel* tableProxy_ = nullptr;

  QVector<Crumb> crumbs_;
  QString currentFolderId_;
  QString currentFolderPath_ = "/";
  QString selectedFileId_;

  QLineEdit* filePathEdit_ = nullptr;
  QPushButton* browseBtn_ = nullptr;
  QLineEdit* passEdit_ = nullptr;
  QLineEdit* pass2Edit_ = nullptr;
  QLineEdit* folderEdit_ = nullptr;
  QPushButton* encryptUploadBtn_ = nullptr;

  QLineEdit* itemIdEdit_ = nullptr;
  QPushButton* downloadDecryptBtn_ = nullptr;

  QProgressBar* progress_ = nullptr;
  QTextEdit* log_ = nullptr;

  QString configPath_;
  QString uploadFolder_;

  QMetaObject::Connection downloadFinishedConn_;
};
