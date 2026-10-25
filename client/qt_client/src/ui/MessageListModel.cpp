#include "MessageListModel.h"

#include <QHash>
#include <QVariant>

MessageListModel::MessageListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int MessageListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
    {
        return 0;
    }
    return m_messages.size();
}

QVariant MessageListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_messages.size())
    {
        return {};
    }

    const ChatMessageRecord &message = m_messages.at(index.row());
    switch (role)
    {
    case Qt::DisplayRole:
    case ContentRole:
        return message.content;
    case ConversationIdRole:
        return QVariant::fromValue<qulonglong>(message.conversationId);
    case ServerMessageIdRole:
        return QVariant::fromValue<qulonglong>(message.serverMessageId);
    case ClientMessageIdRole:
        return message.clientMessageId;
    case SenderIdRole:
        return QVariant::fromValue<qulonglong>(message.senderId);
    case SenderNameRole:
        return message.senderName;
    case SequenceRole:
        return QVariant::fromValue<qulonglong>(message.sequence);
    case TimeTextRole:
        return message.timeText;
    case IsSelfRole:
        return !m_currentUsername.isEmpty() &&
               message.senderName.compare(m_currentUsername, Qt::CaseInsensitive) == 0;
    default:
        return {};
    }
}

QHash<int, QByteArray> MessageListModel::roleNames() const
{
    return {
        {ConversationIdRole, "conversationId"},
        {ServerMessageIdRole, "serverMessageId"},
        {ClientMessageIdRole, "clientMessageId"},
        {SenderIdRole, "senderId"},
        {SenderNameRole, "senderName"},
        {ContentRole, "content"},
        {SequenceRole, "sequence"},
        {TimeTextRole, "timeText"},
        {IsSelfRole, "isSelf"}};
}

void MessageListModel::setCurrentUsername(const QString &username)
{
    if (m_currentUsername == username)
    {
        return;
    }

    m_currentUsername = username;
    if (!m_messages.isEmpty())
    {
        emit dataChanged(index(0, 0), index(m_messages.size() - 1, 0), {IsSelfRole});
    }
}

bool MessageListModel::appendMessage(const ChatMessageRecord &message)
{
    if (message.conversationId == 0 ||
        message.senderId == 0 || message.sequence == 0 ||
        message.senderName.isEmpty() || message.content.isEmpty())
    {
        return false;
    }

    if (containsMessage(message))
    {
        return false;
    }

    // 服务端会话序号 seq 是消息的排序依据。若稍晚到达的推送具有更小的 seq，
    // 将它插入同一会话的正确位置，而不是假设 TCP 到达顺序永远等于数据库提交顺序。
    int insertRow = m_messages.size();
    for (int row = 0; row < m_messages.size(); ++row)
    {
        const ChatMessageRecord &existing = m_messages.at(row);
        if (existing.conversationId == message.conversationId &&
            existing.sequence > message.sequence)
        {
            insertRow = row;
            break;
        }
    }

    beginInsertRows(QModelIndex(), insertRow, insertRow);
    m_messages.insert(insertRow, message);
    endInsertRows();
    return true;
}

void MessageListModel::clearMessages()
{
    if (m_messages.isEmpty() && m_currentUsername.isEmpty())
    {
        return;
    }

    beginResetModel();
    m_messages.clear();
    m_currentUsername.clear();
    endResetModel();
}

bool MessageListModel::containsMessage(const ChatMessageRecord &message) const
{
    for (const ChatMessageRecord &existing : m_messages)
    {
        // 正常情况下 server_msg_id 足以识别一条已保存消息。
        if (message.serverMessageId != 0 &&
            existing.serverMessageId == message.serverMessageId)
        {
            return true;
        }

        // 历史消息接口没有返回 server_msg_id，使用同一会话的 seq 做稳定去重。
        if (existing.conversationId == message.conversationId &&
            existing.sequence == message.sequence)
        {
            return true;
        }

        // 额外用 client_msg_id 识别本端重试导致的重复推送。
        if (!message.clientMessageId.isEmpty() &&
            existing.clientMessageId == message.clientMessageId)
        {
            return true;
        }
    }
    return false;
}
