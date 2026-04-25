#include "MainWindow.h"
#include "../auth/AuthManager.h"
#include "../crypto/CryptoEngine.h"
#include "../util/JsonConfig.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QStandardPaths>
#include <QDateTime>
#include <QHeaderView>
#include <QFileInfo>
#include <QItemSelectionModel>
#include <QRegularExpression>
#include <climits>
#include <algorithm>

static QString humanSize(qint64 bytes) {
  if (bytes < 0) return "";
  const char* units[] = {"B", "KB", "MB", "GB", "TB"};
  double b = (double)bytes;
  int u = 0;
  while (b >= 1024.0 && u < 4) { b /= 1024.0; u++; }
  return QString::number(b, 'f', (u==0?0:1)) + " " + units[u];
}

MainWindow::MainWindow(QWidget* parent)
  : QMainWindow(parent)
{
  auth_ = std::make_unique<AuthManager>(this);
  graph_ = std::make_unique<GraphClient>(&nam_, this);

  setupUi();
  loadConfig();
  initOneDriveBrowser();

  connect(auth_.get(), &AuthManager::signedIn, this, [this](){
    statusLbl_->setText("Signed in");
    graph_->setAccessToken(auth_->accessToken());
    log("Signed in successfully");
    refreshCurrentFolder();
  });
  connect(auth_.get(), &AuthManager::authError, this, [this](const QString& e){
    log("Auth error: " + e);
    QMessageBox::critical(this, "Authentication error", e);
  });

  connect(graph_.get(), &GraphClient::progress, this, [this](qint64 done, qint64 total){
    progress_->setMaximum(total > 0 ? (int)std::min<qint64>(total, INT_MAX) : 0);
    progress_->setValue((int)std::min<qint64>(done, INT_MAX));
  });
  connect(graph_.get(), &GraphClient::finished, this, [this](const QString& m){
    log(m);
    QMessageBox::information(this, "Done", m);
  });
  connect(graph_.get(), &GraphClient::failed, this, [this](const QString& m){
    log("Error: " + m);
    QMessageBox::critical(this, "Error", m);
  });

  connect(graph_.get(), &GraphClient::childrenListed, this, &MainWindow::populateChildren);
}

void MainWindow::setupUi() {
  central_ = new QWidget(this);
  setCentralWidget(central_);

  auto* root = new QVBoxLayout(central_);

  auto* topRow = new QHBoxLayout();
  signInBtn_ = new QPushButton("Sign in");
  statusLbl_ = new QLabel("Not signed in");
  topRow->addWidget(signInBtn_);
  topRow->addWidget(statusLbl_);
  topRow->addStretch();
  root->addLayout(topRow);

  connect(signInBtn_, &QPushButton::clicked, this, &MainWindow::onSignIn);

  auto* contentRow = new QHBoxLayout();
  root->addLayout(contentRow, 1);

  // LEFT: OneDrive browser
  auto* left = new QVBoxLayout();
  contentRow->addLayout(left, 2);

  auto* driveTop = new QHBoxLayout();
  refreshDriveBtn_ = new QPushButton("Refresh");
  upBtn_ = new QPushButton("Up");
  currentFolderLbl_ = new QLabel("OneDrive: /");
  driveTop->addWidget(refreshDriveBtn_);
  driveTop->addWidget(upBtn_);
  driveTop->addWidget(currentFolderLbl_, 1);
  left->addLayout(driveTop);

  // Breadcrumb bar
  breadcrumbBar_ = new QWidget();
  breadcrumbLayout_ = new QHBoxLayout(breadcrumbBar_);
  breadcrumbLayout_->setContentsMargins(0,0,0,0);
  breadcrumbLayout_->setSpacing(4);
  left->addWidget(breadcrumbBar_);

  // Search row
  auto* searchRow = new QHBoxLayout();
  searchEdit_ = new QLineEdit();
  searchEdit_->setPlaceholderText("Search in current folder...");
  clearSearchBtn_ = new QPushButton("Clear");
  clearSearchBtn_->setMaximumWidth(70);
  searchRow->addWidget(searchEdit_, 1);
  searchRow->addWidget(clearSearchBtn_);
  left->addLayout(searchRow);

  oneDriveTree_ = new QTreeView();
  oneDriveTable_ = new QTableView();
  oneDriveTree_->setHeaderHidden(true);
  oneDriveTree_->setMinimumWidth(260);
  oneDriveTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
  oneDriveTable_->setSelectionMode(QAbstractItemView::SingleSelection);
  oneDriveTable_->horizontalHeader()->setStretchLastSection(true);
  oneDriveTable_->verticalHeader()->setVisible(false);
  oneDriveTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);

  auto* browserRow = new QHBoxLayout();
  browserRow->addWidget(oneDriveTree_, 1);
  browserRow->addWidget(oneDriveTable_, 2);
  left->addLayout(browserRow, 1);

  // RIGHT: Encrypt/upload and download/decrypt
  auto* right = new QVBoxLayout();
  contentRow->addLayout(right, 2);

  auto* form = new QFormLayout();

  auto* fileRow = new QHBoxLayout();
  filePathEdit_ = new QLineEdit();
  browseBtn_ = new QPushButton("Browse");
  fileRow->addWidget(filePathEdit_);
  fileRow->addWidget(browseBtn_);
  form->addRow("Local file", fileRow);

  connect(browseBtn_, &QPushButton::clicked, this, [this](){
    const auto path = QFileDialog::getOpenFileName(this, "Select a file");
    if (!path.isEmpty()) filePathEdit_->setText(path);
  });

  passEdit_ = new QLineEdit();
  passEdit_->setEchoMode(QLineEdit::Password);
  pass2Edit_ = new QLineEdit();
  pass2Edit_->setEchoMode(QLineEdit::Password);
  form->addRow("Passphrase", passEdit_);
  form->addRow("Confirm", pass2Edit_);

  folderEdit_ = new QLineEdit();
  form->addRow("Upload folder (path)", folderEdit_);

  right->addLayout(form);

  encryptUploadBtn_ = new QPushButton("Encrypt & Upload");
  right->addWidget(encryptUploadBtn_);
  connect(encryptUploadBtn_, &QPushButton::clicked, this, &MainWindow::onEncryptUpload);

  auto* sep = new QFrame();
  sep->setFrameShape(QFrame::HLine);
  right->addWidget(sep);

  auto* dlForm = new QFormLayout();
  itemIdEdit_ = new QLineEdit();
  itemIdEdit_->setPlaceholderText("Auto-filled when selecting a .odenc file (optional override)");
  dlForm->addRow("Selected itemId", itemIdEdit_);
  right->addLayout(dlForm);

  downloadDecryptBtn_ = new QPushButton("Download & Decrypt Selected (.odenc)");
  right->addWidget(downloadDecryptBtn_);
  connect(downloadDecryptBtn_, &QPushButton::clicked, this, &MainWindow::onDownloadDecrypt);

  right->addStretch();

  progress_ = new QProgressBar();
  progress_->setMinimum(0);
  progress_->setValue(0);
  root->addWidget(progress_);

  log_ = new QTextEdit();
  log_->setReadOnly(true);
  root->addWidget(log_, 1);
}

void MainWindow::initOneDriveBrowser() {
  treeModel_ = new QStandardItemModel(this);
  tableModel_ = new QStandardItemModel(this);
  tableProxy_ = new QSortFilterProxyModel(this);
  tableProxy_->setSourceModel(tableModel_);
  tableProxy_->setFilterKeyColumn(0);
  tableProxy_->setFilterCaseSensitivity(Qt::CaseInsensitive);

  oneDriveTree_->setModel(treeModel_);
  oneDriveTable_->setModel(tableProxy_);
  tableModel_->setHorizontalHeaderLabels({"Name", "Type", "Size"});

  auto* rootItem = new QStandardItem("OneDrive");
  rootItem->setData(QString(), Qt::UserRole + 1); // folderId
  rootItem->setData(true, Qt::UserRole + 2);      // isFolder
  ensureDummyChild(rootItem);
  treeModel_->appendRow(rootItem);
  oneDriveTree_->expand(rootItem->index());

  crumbs_.clear();
  crumbs_.push_back({QString(), "Root"});
  updateBreadcrumbBar();

  connect(refreshDriveBtn_, &QPushButton::clicked, this, [this](){ refreshCurrentFolder(); });

  connect(upBtn_, &QPushButton::clicked, this, [this](){
    if (crumbs_.size() > 1) crumbs_.removeLast();
    currentFolderId_ = crumbs_.last().id;

    QString path = "/";
    for (int i=1;i<crumbs_.size();++i) {
      if (!path.endsWith("/")) path += "/";
      path += crumbs_[i].name;
    }
    currentFolderPath_ = path;

    updateBreadcrumbBar();
    refreshCurrentFolder();
  });

  connect(searchEdit_, &QLineEdit::textChanged, this, [this](const QString& text){
    const QRegularExpression re(QRegularExpression::escape(text), QRegularExpression::CaseInsensitiveOption);
    tableProxy_->setFilterRegularExpression(text.isEmpty() ? QRegularExpression() : re);
  });
  connect(clearSearchBtn_, &QPushButton::clicked, this, [this](){ searchEdit_->clear(); });

  connect(oneDriveTree_, &QTreeView::clicked, this, [this](const QModelIndex& idx){
    auto* item = treeModel_->itemFromIndex(idx);
    if (!item) return;
    if (!item->data(Qt::UserRole + 2).toBool()) return;

    setBreadcrumbsFromTreeItem(item);
    currentFolderId_ = item->data(Qt::UserRole + 1).toString();
    refreshCurrentFolder();
  });

  connect(oneDriveTree_, &QTreeView::expanded, this, [this](const QModelIndex& idx){
    auto* item = treeModel_->itemFromIndex(idx);
    if (!item) return;
    if (!item->data(Qt::UserRole + 2).toBool()) return;

    // Lazy-load if dummy
    if (item->rowCount() == 1 && item->child(0)->data(Qt::UserRole + 99).toBool()) {
      if (!auth_->isSignedIn()) return;
      graph_->setAccessToken(auth_->accessToken());
      graph_->listChildren(item->data(Qt::UserRole + 1).toString());
    }
  });

  connect(oneDriveTable_, &QTableView::doubleClicked, this, [this](const QModelIndex& idx){
    if (!idx.isValid()) return;
    const QModelIndex srcIdx = tableProxy_->mapToSource(idx);
    const int row = srcIdx.row();

    const bool isFolder = tableModel_->item(row, 0)->data(Qt::UserRole + 2).toBool();
    const QString id = tableModel_->item(row, 0)->data(Qt::UserRole + 1).toString();
    const QString name = tableModel_->item(row, 0)->text();

    if (isFolder) openFolder(id, name);
  });

  connect(oneDriveTable_->selectionModel(), &QItemSelectionModel::selectionChanged,
          this, [this](const QItemSelection& sel, const QItemSelection&){
    selectedFileId_.clear();
    if (sel.indexes().isEmpty()) return;

    const QModelIndex proxyIdx = sel.indexes().first();
    const QModelIndex srcIdx = tableProxy_->mapToSource(proxyIdx);
    const int row = srcIdx.row();

    const bool isFolder = tableModel_->item(row, 0)->data(Qt::UserRole + 2).toBool();
    const QString id = tableModel_->item(row, 0)->data(Qt::UserRole + 1).toString();
    const QString name = tableModel_->item(row, 0)->text();

    if (!isFolder) {
      selectedFileId_ = id;
      itemIdEdit_->setText(id);
      downloadDecryptBtn_->setEnabled(name.endsWith(".odenc", Qt::CaseInsensitive));
    } else {
      downloadDecryptBtn_->setEnabled(false);
    }
  });

  downloadDecryptBtn_->setEnabled(false);
}

QStandardItem* MainWindow::ensureDummyChild(QStandardItem* folderItem) {
  if (!folderItem) return nullptr;
  if (folderItem->rowCount() == 0) {
    auto* dummy = new QStandardItem("Loading...");
    dummy->setData(true, Qt::UserRole + 99);
    folderItem->appendRow(dummy);
  }
  return folderItem;
}

void MainWindow::refreshCurrentFolder() {
  if (!auth_->isSignedIn()) {
    log("Not signed in; OneDrive browser disabled.");
    return;
  }
  progress_->setValue(0);
  graph_->setAccessToken(auth_->accessToken());
  graph_->listChildren(currentFolderId_);
  currentFolderLbl_->setText("OneDrive: " + currentFolderPath_);
  updateBreadcrumbBar();
}

void MainWindow::openFolder(const QString& folderId, const QString& folderName) {
  crumbs_.push_back({folderId, folderName});
  updateBreadcrumbBar();

  currentFolderId_ = folderId;
  QString path = "/";
  for (int i=1;i<crumbs_.size();++i) {
    if (!path.endsWith("/")) path += "/";
    path += crumbs_[i].name;
  }
  currentFolderPath_ = path;
  currentFolderLbl_->setText("OneDrive: " + currentFolderPath_);

  refreshCurrentFolder();
}

void MainWindow::populateChildren(const QString& parentId, const QVector<DriveItemInfo>& items) {
  // Update table if current folder
  if (parentId == currentFolderId_) {
    tableModel_->removeRows(0, tableModel_->rowCount());
    for (const auto& it : items) {
      auto* name = new QStandardItem(it.name);
      name->setData(it.id, Qt::UserRole + 1);
      name->setData(it.isFolder, Qt::UserRole + 2);
      auto* type = new QStandardItem(it.isFolder ? "Folder" : "File");
      auto* size = new QStandardItem(it.isFolder ? "" : humanSize(it.size));
      tableModel_->appendRow({name, type, size});
    }
    tableProxy_->invalidate();
    oneDriveTable_->resizeColumnsToContents();
  }

  // Populate tree nodes for this parent (lazy)
  std::function<QStandardItem*(QStandardItem*)> findNode;
  findNode = [&](QStandardItem* node)->QStandardItem* {
    if (!node) return nullptr;
    if (node->data(Qt::UserRole + 2).toBool() && node->data(Qt::UserRole + 1).toString() == parentId)
      return node;
    for (int r=0; r<node->rowCount(); ++r) {
      if (auto* f = findNode(node->child(r))) return f;
    }
    return nullptr;
  };

  auto* rootNode = treeModel_->invisibleRootItem()->child(0);
  auto* parentNode = findNode(rootNode);
  if (!parentNode) return;

  if (parentNode->rowCount() == 1 && parentNode->child(0)->data(Qt::UserRole + 99).toBool())
    parentNode->removeRow(0);

  for (const auto& it : items) {
    if (!it.isFolder) continue;
    bool exists = false;
    for (int r=0; r<parentNode->rowCount(); ++r) {
      if (parentNode->child(r)->data(Qt::UserRole + 1).toString() == it.id) { exists = true; break; }
    }
    if (exists) continue;

    auto* child = new QStandardItem(it.name);
    child->setData(it.id, Qt::UserRole + 1);
    child->setData(true, Qt::UserRole + 2);
    ensureDummyChild(child);
    parentNode->appendRow(child);
  }
}

void MainWindow::setBreadcrumbsFromTreeItem(QStandardItem* item) {
  QVector<Crumb> c;
  c.push_back({QString(), "Root"});

  QList<QStandardItem*> chain;
  QStandardItem* cur = item;
  while (cur && cur->parent()) {
    chain.prepend(cur);
    cur = cur->parent();
  }

  for (auto* it : chain) {
    const QString name = it->text();
    const QString id = it->data(Qt::UserRole + 1).toString();
    if (name == "OneDrive") continue;
    if (!id.isEmpty()) c.push_back({id, name});
  }

  crumbs_ = c;
  QString path = "/";
  for (int i=1;i<crumbs_.size();++i) {
    if (!path.endsWith("/")) path += "/";
    path += crumbs_[i].name;
  }
  currentFolderPath_ = path;
  updateBreadcrumbBar();
}

void MainWindow::updateBreadcrumbBar() {
  if (!breadcrumbLayout_) return;

  QLayoutItem* child;
  while ((child = breadcrumbLayout_->takeAt(0)) != nullptr) {
    if (child->widget()) child->widget()->deleteLater();
    delete child;
  }

  for (int i=0; i<crumbs_.size(); ++i) {
    auto* btn = new QToolButton();
    btn->setText(crumbs_[i].name);
    btn->setAutoRaise(true);
    btn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    btn->setCursor(Qt::PointingHandCursor);
    breadcrumbLayout_->addWidget(btn);

    connect(btn, &QToolButton::clicked, this, [this, i](){ navigateToCrumb(i); });

    if (i < crumbs_.size() - 1) {
      auto* sep = new QLabel("/");
      sep->setStyleSheet("color: #888;");
      breadcrumbLayout_->addWidget(sep);
    }
  }
  breadcrumbLayout_->addStretch();
}

void MainWindow::navigateToCrumb(int index) {
  if (index < 0 || index >= crumbs_.size()) return;
  crumbs_.resize(index + 1);
  currentFolderId_ = crumbs_.last().id;

  QString path = "/";
  for (int i=1;i<crumbs_.size();++i) {
    if (!path.endsWith("/")) path += "/";
    path += crumbs_[i].name;
  }
  currentFolderPath_ = path;

  currentFolderLbl_->setText("OneDrive: " + currentFolderPath_);
  updateBreadcrumbBar();
  refreshCurrentFolder();
}

void MainWindow::loadConfig() {
  const auto base = QCoreApplication::applicationDirPath();
  configPath_ = base + "/appconfig.json";

  QString err;
  const auto cfg = JsonConfig::load(configPath_, &err);
  if (!err.isEmpty()) log("Config load warning: " + err + " (using defaults)");

  uploadFolder_ = cfg.defaultUploadFolder;
  folderEdit_->setText(uploadFolder_);

  auth_->configure(cfg.clientId, cfg.tenant, cfg.redirectPort, cfg.scopes);
  log("Loaded config from: " + configPath_);
}

void MainWindow::log(const QString& msg) {
  const auto line = QDateTime::currentDateTime().toString(Qt::ISODate) + "  " + msg;
  log_->append(line);
}

void MainWindow::onSignIn() {
  log("Starting sign-in...");
  auth_->signIn();
}

void MainWindow::onEncryptUpload() {
  if (!auth_->isSignedIn()) { QMessageBox::warning(this, "Not signed in", "Please sign in first."); return; }

  const QString inPath = filePathEdit_->text().trimmed();
  if (inPath.isEmpty()) { QMessageBox::warning(this, "Missing file", "Select a file."); return; }

  const QString p1 = passEdit_->text();
  const QString p2 = pass2Edit_->text();
  if (p1.isEmpty() || p1 != p2) { QMessageBox::warning(this, "Passphrase", "Passphrases are empty or do not match."); return; }

  const QString folder = folderEdit_->text().trimmed();
  if (folder.isEmpty()) { QMessageBox::warning(this, "Folder", "Provide an upload folder path."); return; }

  const auto tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
  const QFileInfo fi(inPath);
  const QString encPath = tempDir + "/" + fi.fileName() + ".odenc";

  try {
    log("Encrypting to: " + encPath);
    CryptoEngine::encryptFile(inPath, encPath, p1);
  } catch (const std::exception& ex) {
    QMessageBox::critical(this, "Encryption failed", ex.what());
    return;
  }

  log("Uploading encrypted file...");
  progress_->setValue(0);
  graph_->setAccessToken(auth_->accessToken());
  graph_->uploadLargeFileToPath(encPath, folder, fi.fileName() + ".odenc");
}

void MainWindow::onDownloadDecrypt() {
  if (!auth_->isSignedIn()) { QMessageBox::warning(this, "Not signed in", "Please sign in first."); return; }

  const QString itemId = !selectedFileId_.isEmpty() ? selectedFileId_ : itemIdEdit_->text().trimmed();
  if (itemId.isEmpty()) { QMessageBox::warning(this, "Missing selection", "Select a .odenc file from OneDrive browser."); return; }

  const QString pass = passEdit_->text();
  if (pass.isEmpty()) { QMessageBox::warning(this, "Passphrase", "Enter the passphrase used to encrypt."); return; }

  const QString outDir = QFileDialog::getExistingDirectory(this, "Choose output folder");
  if (outDir.isEmpty()) return;

  const auto tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
  const QString encPath = tempDir + "/downloaded.odenc";

  log("Downloading item content...");
  progress_->setValue(0);

  graph_->setAccessToken(auth_->accessToken());

  if (downloadFinishedConn_) QObject::disconnect(downloadFinishedConn_);
  downloadFinishedConn_ = connect(graph_.get(), &GraphClient::finished, this, [this, encPath, outDir, pass](const QString& msg){
    if (!msg.startsWith("Download")) return;

    QString suggested = CryptoEngine::peekOriginalName(encPath);
    if (suggested.isEmpty()) suggested = "decrypted_output";

    const QString defaultPath = outDir + "/" + suggested;
    const QString outPath = QFileDialog::getSaveFileName(this, "Save decrypted file", defaultPath, "All Files (*)");
    if (outPath.isEmpty()) {
      log("Decryption cancelled by user.");
      return;
    }

    try {
      log("Decrypting to: " + outPath);
      CryptoEngine::decryptFile(encPath, outPath, pass);
      QMessageBox::information(this, "Decrypted", "Decrypted to: " + outPath);
    } catch (const std::exception& ex) {
      QMessageBox::critical(this, "Decryption failed", ex.what());
    }
  });

  graph_->downloadItemContent(itemId, encPath);
}
