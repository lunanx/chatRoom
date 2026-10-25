#pragma once

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QVariant>
#include <QString>
#include <QVector>

#include "model/ChatMessageRecord.h"

// 消息列表的数据模型：保存已经从服务器接收到的消息，并向视图提供角色数据。
class MessageListModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role
    {
        ConversationIdRole = Qt::UserRole + 1,
        ServerMessageIdRole,
        ClientMessageIdRole,
        SenderIdRole,
        SenderNameRole,
        ContentRole,
        SequenceRole,
        TimeTextRole,
        IsSelfRole
    };

    explicit MessageListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    // 记录当前登录用户名，以便把自己发送的消息画在右侧。
    void setCurrentUsername(const QString &username);

    // 保存一条服务器确认过的消息。重复推送同一 server_msg_id 时不会插入第二行。
    // 返回 true 表示新消息已加入模型，false 表示该消息无效或已经存在。
    bool appendMessage(const ChatMessageRecord &message);
    // 退出登录或连接失效时清除内存中的消息，避免账号切换后残留上一账号的数据。
    void clearMessages();

private:
    bool containsMessage(const ChatMessageRecord &message) const;

    QVector<ChatMessageRecord> m_messages;
    QString m_currentUsername;
};
