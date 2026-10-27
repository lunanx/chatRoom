#pragma once

#include <QWidget>
#include <QString>
#include <QVector>
#include <QtGlobal>

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTabWidget;

// 好友列表项来自服务端 QUERY_FRIEND_LIST_SUCCESS 响应。
struct ContactItem
{
    quint64 userId = 0;
    QString username;
    QString nickname;
    QString avatar;
    QString createdAt;
};

// 待处理申请来自服务端 QUERY_PENDING_FRIEND_LIST_SUCCESS 响应。
struct PendingFriendRequestItem
{
    quint64 requestId = 0;
    quint64 senderId = 0;
    QString username;
    QString nickname;
    QString avatar;
    QString createdAt;
};

// 联系人页只负责显示数据和发出用户操作，不直接访问 TCP 或协议。
class ContactsPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ContactsPanel(QWidget *parent = nullptr);

    void setFriends(const QVector<ContactItem> &friends);
    void setPendingRequests(const QVector<PendingFriendRequestItem> &requests);
    void setStatus(const QString &message, bool isError = false);
    void finishAddFriendRequest(bool clearInput);

signals:
    void refreshRequested();
    void sendFriendRequestRequested(const QString &username);
    void handleFriendRequestRequested(quint64 requestId, quint64 senderUserId, int action);
    void openPrivateChatRequested(quint64 userId);

private:
    void submitFriendRequest();
    void rebuildFriendRows();
    void rebuildRequestRows();

    QLineEdit *m_usernameEdit = nullptr;
    QPushButton *m_addButton = nullptr;
    QLabel *m_statusLabel = nullptr;
    QListWidget *m_friendList = nullptr;
    QListWidget *m_requestList = nullptr;
    QTabWidget *m_tabs = nullptr;

    QVector<ContactItem> m_friends;
    QVector<PendingFriendRequestItem> m_pendingRequests;
};
