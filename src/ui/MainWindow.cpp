#include "MainWindow.h"

#include "../auth/AuthManager.h"
#include "../crypto/CryptoEngine.h"
#include "../util/JsonConfig.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QSplitter>
#include <QGroupBox>
#include <QFileDialog>
#include <QMessageBox>
#include <QStandardPaths>
#include <QDateTime>
#include <QHeaderView>
#include <QFileInfo>
#include <QItemSelectionModel>
#include <QFrame>
#include <QLayoutItem>
#include <algorithm>

MainWindow::MainWindow(QWidget* parent)
  : QMainWindow(parent)
{
  setupUi();
  loadConfig();
}

MainWindow::~MainWindow() = default;

// ---------------------------------------------------------------------------
// UI construction
// ---------------------------------------------------------------------------

void MainWindow::setupUi() {
  setWindowTitle("OneDrive E2E Encryptor/Decryptor");

  central_ = new QWidget(this);
  setCentralWidget(central_);
  auto* mainLayout = new QVBoxLayout(central_);

  // ---- Top bar: sign-in, status ----
  {
    auto* bar = new QHBoxLayout();
    signInBtn_ = new QPushButton("Sign In", this);
    statusLbl_ = new QLabel("Not signed in", this);
    bar->addWidget(signInBtn_);
    bar->addWidget(statusLbl_);
    bar->addStretch();
    mainLayout->addLayout(bar);
  }

  // ---- Breadcrumb bar ----
  breadcrumbBar_ = new QWidget(this);
  breadcrumbLayout_ = new QHBoxLayout(breadcrumbBar_);
  breadcrumbLayout_->setContentsMargins(0, 0, 0, 0);
  breadcrumbLayout_->addStretch();
  mainLayout->addWidget(breadcrumbBar_);

  // ---- Search bar ----
  {
    auto* bar = new QHBoxLayout();
    searchEdit_ = new QLineEdit(this);
    searchEdit_->setPlaceholderText("Search in current folder...");
    clearSearchBtn_ = new QPushButton("Clear", this);
    bar->addWidget(new QLabel("Search:", this));
    bar->addWidget(searchEdit_);
    bar->addWidget(clearSearchBtn_);
    mainLayout->addLayout(bar);
  }

  // ---- OneDrive browser controls ----
  {
    auto* bar = new QHBoxLayout();
    refreshDriveBtn_ = new QPushButton("Refresh", this);
    upBtn_ = new QPushButton("Up", this);
    currentFolderLbl_ = new QLabel("/", this);
    bar->addWidget(refreshDriveBtn_);
    bar->addWidget(upBtn_);
    bar->addWidget(currentFolderLbl_);
    bar->addStretch();
    mainLayout->addLayout(bar);
  }

  // ---- Split tree / table ----
  {
    auto* splitter = new QSplitter(Qt::Horizontal, this);

    oneDriveTree_ = new QTreeView(this);
    oneDriveTable_ = new QTableView(this);

    splitter->addWidget(oneDriveTree_);
    splitter->addWidget(oneDriveTable_);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 3);
    mainLayout->addWidget(splitter, 2);

    treeModel_ = new QStandardItemModel(this);
    treeModel_->setHorizontalHeaderLabels({"OneDrive"});
    oneDriveTree_->setModel(treeModel_);

    tableModel_ = new QStandardItemModel(this);
    tableModel_->setHorizontalHeaderLabels({"Name", "Type", "Size"});

    tableProxy_ = new QSortFilterProxyModel(this);
    tableProxy_->setSourceModel(tableModel_);
    tableProxy_->setFilterCaseSensitivity(Qt::CaseInsensitive);
    tableProxy_->setFilterKeyColumn(0);

    oneDriveTable_->setModel(tableProxy_);
    oneDriveTable_->horizontalHeader()->setStretchLastSection(true);
    oneDriveTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    oneDriveTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  }

  // ---- Encrypt & Upload ----
  {
    auto* grp = new QGroupBox("Encrypt & Upload", this);
    auto* fl = new QFormLayout(grp);

    filePathEdit_ = new QLineEdit(this);
    browseBtn_ = new QPushButton("Browse...", this);
    auto* fileRow = new QHBoxLayout();
    fileRow->addWidget(filePathEdit_);
    fileRow->addWidget(browseBtn_);
    fl->addRow("Local file:", fileRow);

    passEdit_ = new QLineEdit(this);
    passEdit_->setEchoMode(QLineEdit::Password);
    fl->addRow("Password:", passEdit_);

    pass2Edit_ = new QLineEdit(this);
    pass2Edit_->setEchoMode(QLineEdit::Password);
    fl->addRow("Confirm:", pass2Edit_);

    folderEdit_ = new QLineEdit(this);
    fl->addRow("Upload folder:", folderEdit_);

    encryptUploadBtn_ = new QPushButton("Encrypt && Upload", this);
    fl->addRow("", encryptUploadBtn_);

    mainLayout->addWidget(grp);
  }

  // ---- Download & Decrypt ----
  {
    auto* grp = new QGroupBox("Download & Decrypt", this);
    auto* fl = new QFormLayout(grp);

    itemIdEdit_ = new QLineEdit(this);
    fl->addRow("Item ID:", itemIdEdit_);

    downloadDecryptBtn_ = new QPushButton("Download && Decrypt", this);
    fl->addRow("", downloadDecryptBtn_);

    mainLayout->addWidget(grp);
  }

  // ---- Progress + log ----
  progress_ = new QProgressBar(this);
  progress_->setRange(0, 100);
  progress_->setValue(0);
  mainLayout->addWidget(progress_);

  log_ = new QTextEdit(this);
  log_->setReadOnly(true);
  mainLayout->addWidget(log_, 1);

  // ---- Signal connections ----
  connect(signInBtn_, &QPushButton::clicked, this, &MainWindow::onSignIn);

  connect(browseBtn_, &QPushButton::clicked, this, [this]() {
    const QString p = QFileDialog::getOpenFileName(this, "Select file to encrypt");
    if (!p.isEmpty()) filePathEdit_->setText(p);
  });

  connect(encryptUploadBtn_, &QPushButton::clicked, this, &MainWindow::onEncryptUpload);
  connect(downloadDecryptBtn_, &QPushButton::clicked, this, &MainWindow::onDownloadDecrypt);
  connect(refreshDriveBtn_, &QPushButton::clicked, this, &MainWindow::refreshCurrentFolder);

  connect(upBtn_, &QPushButton::clicked, this, [this]() {
    if (crumbs_.size() > 1) navigateToCrumb((int)crumbs_.size() - 2);
  });

  connect(clearSearchBtn_, &QPushButton::clicked, this, [this]() {
    searchEdit_->clear();
  });
  connect(searchEdit_, &QLineEdit::textChanged, tableProxy_,
          &QSortFilterProxyModel::setFilterFixedString);

  // Double-click a row: open folder or fill item-ID field
  connect(oneDriveTable_, &QAbstractItemView::doubleClicked,
          this, [this](const QModelIndex& proxyIdx) {
    const QModelIndex srcIdx = tableProxy_->mapToSource(proxyIdx);
    if (!srcIdx.isValid()) return;
    auto* item = tableModel_->item(srcIdx.row(), 0);
    if (!item) return;
    const bool isFolder = item->data(Qt::UserRole + 1).toBool();
    const QString id   = item->data(Qt::UserRole + 2).toString();
    const QString name = item->text();
    if (isFolder) openFolder(id, name);
    else itemIdEdit_->setText(id);
  });

  // Single-click: populate item-ID field
  connect(oneDriveTable_->selectionModel(), &QItemSelectionModel::selectionChanged,
          this, [this](const QItemSelection& sel, const QItemSelection&) {
    if (sel.indexes().isEmpty()) return;
    const QModelIndex srcIdx = tableProxy_->mapToSource(sel.indexes().first());
    if (!srcIdx.isValid()) return;
    auto* item = tableModel_->item(srcIdx.row(), 0);
    if (!item) return;
    selectedFileId_ = item->data(Qt::UserRole + 2).toString();
    itemIdEdit_->setText(selectedFileId_);
  });

  // Tree click: navigate to folder
  connect(oneDriveTree_, &QTreeView::clicked, this, [this](const QModelIndex& idx) {
    auto* item = treeModel_->itemFromIndex(idx);
    if (!item) return;
    const QString id   = item->data(Qt::UserRole + 2).toString();
    const QString name = item->text();
    setBreadcrumbsFromTreeItem(item);
    currentFolderId_ = id;
    currentFolderPath_ = name;
    currentFolderLbl_->setText(name);
    refreshCurrentFolder();
  });
}

// ---------------------------------------------------------------------------
// Configuration loading
// ---------------------------------------------------------------------------

void MainWindow::loadConfig() {
  // Try local config first, fall back to app-config location
  const QString localPath = "appconfig.json";
  if (QFile::exists(localPath)) {
    configPath_ = localPath;
  } else {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    configPath_ = dir + "/appconfig.json";
  }

  QString err;
  AppConfig cfg = JsonConfig::load(configPath_, &err);

  uploadFolder_ = cfg.defaultUploadFolder;
  if (!uploadFolder_.isEmpty()) folderEdit_->setText(uploadFolder_);

  if (cfg.clientId.isEmpty()) {
    log("No appconfig.json found — sign-in unavailable until configured.");
    signInBtn_->setEnabled(false);
    return;
  }

  auth_ = std::make_unique<AuthManager>(this);
  auth_->configure(cfg.clientId, cfg.tenant, cfg.redirectPort, cfg.scopes);

  connect(auth_.get(), &AuthManager::signedIn, this, [this]() {
    statusLbl_->setText("Signed in");
    signInBtn_->setText("Sign Out");
    log("Signed in successfully.");

    graph_ = std::make_unique<GraphClient>(&nam_, this);
    graph_->setAccessToken(auth_->accessToken());

    connect(graph_.get(), &GraphClient::progress, this, [this](qint64 done, qint64 total) {
      if (total > 0) progress_->setValue((int)((done * 100) / total));
    });
    connect(graph_.get(), &GraphClient::finished, this, [this](const QString& msg) {
      log(msg);
      progress_->setValue(0);
    });
    connect(graph_.get(), &GraphClient::failed, this, [this](const QString& msg) {
      log("Error: " + msg);
      progress_->setValue(0);
    });
    connect(graph_.get(), &GraphClient::childrenListed,
            this, &MainWindow::populateChildren);

    initOneDriveBrowser();
  });

  connect(auth_.get(), &AuthManager::signedOut, this, [this]() {
    statusLbl_->setText("Not signed in");
    signInBtn_->setText("Sign In");
    log("Signed out.");
  });

  connect(auth_.get(), &AuthManager::authError, this, [this](const QString& e) {
    log("Auth error: " + e);
    QMessageBox::warning(this, "Auth Error", e);
  });
}

// ---------------------------------------------------------------------------
// Logging
// ---------------------------------------------------------------------------

void MainWindow::log(const QString& msg) {
  log_->append("[" + QDateTime::currentDateTime().toString("hh:mm:ss") + "] " + msg);
}

// ---------------------------------------------------------------------------
// Auth
// ---------------------------------------------------------------------------

void MainWindow::onSignIn() {
  if (!auth_) {
    QMessageBox::warning(this, "Not configured",
                         "No appconfig.json found.\nCreate one from appconfig.example.json.");
    return;
  }
  if (auth_->isSignedIn()) auth_->signOut();
  else auth_->signIn();
}

// ---------------------------------------------------------------------------
// Encrypt & upload
// ---------------------------------------------------------------------------

void MainWindow::onEncryptUpload() {
  if (!graph_ || !auth_ || !auth_->isSignedIn()) {
    QMessageBox::warning(this, "Not signed in", "Please sign in first.");
    return;
  }
  const QString filePath = filePathEdit_->text().trimmed();
  if (filePath.isEmpty()) {
    QMessageBox::warning(this, "No file", "Please select a file to encrypt.");
    return;
  }
  const QString pass  = passEdit_->text();
  const QString pass2 = pass2Edit_->text();
  if (pass.isEmpty()) {
    QMessageBox::warning(this, "No password", "Please enter a password.");
    return;
  }
  if (pass != pass2) {
    QMessageBox::warning(this, "Password mismatch", "Passwords do not match.");
    return;
  }

  const QString folder = folderEdit_->text().trimmed().isEmpty()
    ? uploadFolder_ : folderEdit_->text().trimmed();

  const QString baseName = QFileInfo(filePath).fileName();
  const QString encName  = baseName + ".odenc";
  const QString tempPath = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                           + "/" + encName;

  try {
    log("Encrypting " + baseName + "…");
    CryptoEngine::encryptFile(filePath, tempPath, pass);
    log("Encryption done. Uploading as " + encName + " to " + folder + " …");
    graph_->uploadLargeFileToPath(tempPath, folder, encName);
  } catch (const std::exception& ex) {
    log(QString("Encryption failed: ") + ex.what());
    QMessageBox::critical(this, "Encryption error", ex.what());
  }
}

// ---------------------------------------------------------------------------
// Download & decrypt
// ---------------------------------------------------------------------------

void MainWindow::onDownloadDecrypt() {
  if (!graph_ || !auth_ || !auth_->isSignedIn()) {
    QMessageBox::warning(this, "Not signed in", "Please sign in first.");
    return;
  }
  const QString itemId = itemIdEdit_->text().trimmed();
  if (itemId.isEmpty()) {
    QMessageBox::warning(this, "No item", "Please enter or select an item ID.");
    return;
  }
  const QString pass = passEdit_->text();
  if (pass.isEmpty()) {
    QMessageBox::warning(this, "No password", "Please enter the decryption password.");
    return;
  }

  const QString tempPath = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                           + "/downloaded.odenc";

  // Disconnect any stale connection from a previous download
  if (downloadFinishedConn_) QObject::disconnect(downloadFinishedConn_);

  downloadFinishedConn_ = connect(graph_.get(), &GraphClient::finished,
    this, [this, tempPath, pass](const QString& msg) {
      if (!msg.contains("Download")) return;
      QObject::disconnect(downloadFinishedConn_);
      log("Download complete. Decrypting…");

      QString suggestedName = CryptoEngine::peekOriginalName(tempPath);
      if (suggestedName.isEmpty()) suggestedName = "decrypted_file";

      const QString savePath = QFileDialog::getSaveFileName(
        this, "Save decrypted file",
        QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)
          + "/" + suggestedName);
      if (savePath.isEmpty()) { log("Decryption cancelled."); return; }

      try {
        CryptoEngine::decryptFile(tempPath, savePath, pass);
        log("Decrypted successfully: " + savePath);
      } catch (const std::exception& ex) {
        log(QString("Decryption failed: ") + ex.what());
        QMessageBox::critical(this, "Decryption error", ex.what());
      }
    });

  log("Downloading item " + itemId + "…");
  graph_->downloadItemContent(itemId, tempPath);
}

// ---------------------------------------------------------------------------
// OneDrive browser
// ---------------------------------------------------------------------------

void MainWindow::initOneDriveBrowser() {
  treeModel_->clear();
  treeModel_->setHorizontalHeaderLabels({"OneDrive"});

  auto* root = new QStandardItem("My Drive");
  root->setData(QString(), Qt::UserRole + 2);
  root->setData(true, Qt::UserRole + 1);
  treeModel_->appendRow(root);
  ensureDummyChild(root);

  // Lazy-load tree children on expand
  connect(oneDriveTree_, &QTreeView::expanded, this, [this](const QModelIndex& idx) {
    auto* item = treeModel_->itemFromIndex(idx);
    if (!item) return;
    // Placeholder row signals that this node hasn't been loaded yet
    if (item->rowCount() == 1 && item->child(0)->text() == "…") {
      item->removeRows(0, 1);
      const QString id = item->data(Qt::UserRole + 2).toString();
      graph_->listChildren(id);
    }
  });

  crumbs_ = {{ QString(), "My Drive" }};
  updateBreadcrumbBar();
  refreshCurrentFolder();
}

void MainWindow::refreshCurrentFolder() {
  if (!graph_) return;
  tableModel_->setRowCount(0);
  graph_->listChildren(currentFolderId_);
}

void MainWindow::openFolder(const QString& folderId, const QString& folderName) {
  currentFolderId_   = folderId;
  currentFolderPath_ = folderName;
  currentFolderLbl_->setText(folderName);
  crumbs_.append({ folderId, folderName });
  updateBreadcrumbBar();
  refreshCurrentFolder();
}

void MainWindow::populateChildren(const QString& parentId,
                                  const QVector<DriveItemInfo>& items)
{
  // ---- Update table when response is for the current folder ----
  if (parentId == currentFolderId_) {
    tableModel_->setRowCount(0);
    for (const auto& di : items) {
      auto* nameItem = new QStandardItem(di.name);
      nameItem->setData(di.isFolder, Qt::UserRole + 1);
      nameItem->setData(di.id,       Qt::UserRole + 2);
      auto* typeItem = new QStandardItem(di.isFolder ? "Folder" : "File");
      auto* sizeItem = new QStandardItem(di.size >= 0 ? QString::number(di.size) : "-");
      tableModel_->appendRow({ nameItem, typeItem, sizeItem });
    }
  }

  // ---- Update tree: populate child folders under the matching tree node ----
  const auto findById = [&](const QString& id) -> QStandardItem* {
    const QList<QStandardItem*> all =
      treeModel_->findItems("*", Qt::MatchWildcard | Qt::MatchRecursive);
    for (auto* it : all) {
      if (it->data(Qt::UserRole + 2).toString() == id) return it;
    }
    return nullptr;
  };

  QStandardItem* parent = findById(parentId);
  if (parent) {
    // Remove placeholder if still there
    if (parent->rowCount() == 1 && parent->child(0)->text() == "…")
      parent->removeRows(0, 1);

    for (const auto& di : items) {
      if (!di.isFolder) continue;
      // Only add if not already in tree
      if (!findById(di.id)) {
        auto* child = new QStandardItem(di.name);
        child->setData(true,  Qt::UserRole + 1);
        child->setData(di.id, Qt::UserRole + 2);
        parent->appendRow(child);
        ensureDummyChild(child);
      }
    }
  }
}

QStandardItem* MainWindow::ensureDummyChild(QStandardItem* folderItem) {
  if (folderItem->rowCount() == 0) {
    auto* dummy = new QStandardItem("…");
    folderItem->appendRow(dummy);
    return dummy;
  }
  return nullptr;
}

// ---------------------------------------------------------------------------
// Breadcrumbs
// ---------------------------------------------------------------------------

void MainWindow::updateBreadcrumbBar() {
  // Clear existing widgets
  while (QLayoutItem* li = breadcrumbLayout_->takeAt(0)) {
    if (QWidget* w = li->widget()) w->deleteLater();
    delete li;
  }

  for (int i = 0; i < crumbs_.size(); ++i) {
    if (i > 0) {
      auto* sep = new QLabel(">", breadcrumbBar_);
      breadcrumbLayout_->addWidget(sep);
    }
    auto* btn = new QToolButton(breadcrumbBar_);
    btn->setText(crumbs_[i].name);
    const int idx = i;
    connect(btn, &QToolButton::clicked, this, [this, idx]() { navigateToCrumb(idx); });
    breadcrumbLayout_->addWidget(btn);
  }
  breadcrumbLayout_->addStretch();
}

void MainWindow::navigateToCrumb(int index) {
  if (index < 0 || index >= crumbs_.size()) return;
  crumbs_.resize(index + 1);
  const auto& c = crumbs_[index];
  currentFolderId_   = c.id;
  currentFolderPath_ = c.name;
  currentFolderLbl_->setText(c.name);
  updateBreadcrumbBar();
  refreshCurrentFolder();
}

void MainWindow::setBreadcrumbsFromTreeItem(QStandardItem* item) {
  crumbs_.clear();
  QList<QStandardItem*> path;
  for (QStandardItem* cur = item; cur; cur = cur->parent())
    path.prepend(cur);
  for (auto* p : path)
    crumbs_.append({ p->data(Qt::UserRole + 2).toString(), p->text() });
  updateBreadcrumbBar();
}
