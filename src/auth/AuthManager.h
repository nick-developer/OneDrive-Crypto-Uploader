#pragma once

#include <QObject>
#include <QOAuth2AuthorizationCodeFlow>
#include <QOAuthHttpServerReplyHandler>

class AuthManager : public QObject {
  Q_OBJECT
public:
  explicit AuthManager(QObject* parent = nullptr);

  void configure(const QString& clientId,
                 const QString& tenant,
                 int redirectPort,
                 const QStringList& scopes);

  void signIn();
  void signOut();

  QString accessToken() const;
  bool isSignedIn() const;

signals:
  void signedIn();
  void signedOut();
  void authError(const QString&);

private:
  QOAuth2AuthorizationCodeFlow oauth_;
  QOAuthHttpServerReplyHandler* replyHandler_ = nullptr;
};
