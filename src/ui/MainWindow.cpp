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
#include <QFrame>
#include <QLayoutItem>
#include <climits>
#include <algorithm>

MainWindow::~MainWindow() = default;

// NOTE: This file only contains the destructor definition required to fix
// the incomplete-type std::unique_ptr<AuthManager> issue.
// Merge this destructor definition into your existing MainWindow.cpp.
