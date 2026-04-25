#include "AuthManager.h"
#include <QDesktopServices>

AuthManager::AuthManager(QObject* parent)
  : QObject(parent), replyHandler_(this)
{
  oauth_.setReplyHandler(&replyHandler_);
  oauth_.setPkceMethod(QOAuth2AuthorizationCodeFlow::PkceMethod::S256);

  connect(&oauth_, &QOAuth2AuthorizationCodeFlow::authorizeWithBrowser,
          this, [](const QUrl& url){ QDesktopServices::openUrl(url); });

  connect(&oauth_, &QOAuth2AuthorizationCodeFlow::granted,
          this, &AuthManager::signedIn);

  connect(&oauth_, &QOAuth2AuthorizationCodeFlow::error,
          this, [this](const QString& error, const QString& desc, const QUrl&){
            emit authError(error + ": " + desc);
          });
}

void AuthManager::configure(const QString& clientId,
                            const QString& tenant,
                            int redirectPort,
                            const QStringList& scopes)
{
  replyHandler_.setListenPort(redirectPort);

  oauth_.setClientIdentifier(clientId);

  const QUrl authUrl(QString("https://login.microsoftonline.com/%1/oauth2/v2.0/authorize").arg(tenant));
  const QUrl tokenUrl(QString("https://login.microsoftonline.com/%1/oauth2/v2.0/token").arg(tenant));

  oauth_.setAuthorizationUrl(authUrl);
  oauth_.setAccessTokenUrl(tokenUrl);

  oauth_.setScope(scopes.join(' '));
  oauth_.setModifyParametersFunction([](QAbstractOAuth::Stage stage, QMultiMap<QString, QVariant>* params) {
    if (stage == QAbstractOAuth::Stage::RequestingAuthorization) {
      params->insert("response_mode", "query");
    }
  });
}

void AuthManager::signIn() { oauth_.grant(); }

void AuthManager::signOut() {
  oauth_.setToken(QString());
  emit signedOut();
}

QString AuthManager::accessToken() const { return oauth_.token(); }
bool AuthManager::isSignedIn() const { return !oauth_.token().isEmpty(); }
