#pragma once

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QString>
#include <QVariant>
#include <QVector>

#include "model/ChatMessageRecord.h"

// 消息列表的数据模型：保存消息，并向 QListView/代理模型/绘制代理提供角色数据。
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
        IsSelfRole,
        SendStateRole
    };

    explicit MessageListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    // 记录当前登录用户名，以便把自己发送的消息画在右侧。
    void setCurrentUsername(const QString &username);

    // 插入一条消息。
    // Pending 消息允许 senderId、serverMessageId、sequence 暂时为 0；
    // 服务器消息必须具有有效的 senderId 和 sequence。
    // 如果服务端推送先于发送响应到达，会用推送内容补全对应的本地 Pending 消息。
    bool appendMessage(const ChatMessageRecord &message);

    // 按 client_msg_id 更新本地消息状态。找不到对应消息时返回 false。
    bool setSendState(const QString &clientMessageId, MessageSendState state);

    // 用发送响应中的 server_msg_id/seq 确认本地 Pending 消息，并按 seq 重新排序。
    bool confirmPendingMessage(const QString &clientMessageId,
                               quint64 serverMessageId,
                               quint64 sequence);

    // 退出登录或连接失效时清除内存中的消息。
    void clearMessages();

private:
    bool containsMessage(const ChatMessageRecord &message) const;
    int findMessageByClientMessageId(const QString &clientMessageId) const;
    void moveMessageToSequenceOrder(int row);

    QVector<ChatMessageRecord> m_messages;
    QString m_currentUsername;
};
