#include "MessageListModel.h"

#include <QVector>

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
    case SendStateRole:
        return static_cast<int>(message.sendState);
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
        {IsSelfRole, "isSelf"},
        {SendStateRole, "sendState"}};
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
    if (message.conversationId == 0 || message.senderName.trimmed().isEmpty() ||
        message.content.isEmpty())
    {
        return false;
    }

    const bool isPending = message.sendState == MessageSendState::Pending;
    if (isPending)
    {
        // 待确认消息依靠 client_msg_id 与服务器响应/推送匹配。
        if (message.clientMessageId.isEmpty())
        {
            return false;
        }
    }
    else if (message.senderId == 0 || message.sequence == 0)
    {
        // 服务器确认过的消息必须有发送者 ID 和会话内序号。
        return false;
    }

    // 如果本地 Pending 气泡已存在，而服务端推送先到达，直接补全原气泡，
    // 避免同一条消息出现两次，也避免它一直停留在 sequence=0 的位置。
    if (!message.clientMessageId.isEmpty())
    {
        const int existingRow = findMessageByClientMessageId(message.clientMessageId);
        if (existingRow >= 0)
        {
            ChatMessageRecord &existing = m_messages[existingRow];
            if (existing.sendState == MessageSendState::Pending && !isPending &&
                message.senderId != 0 && message.sequence != 0)
            {
                existing = message;
                existing.sendState = MessageSendState::Sent;
                moveMessageToSequenceOrder(existingRow);
                const int updatedRow = findMessageByClientMessageId(message.clientMessageId);
                if (updatedRow >= 0)
                {
                    emit dataChanged(index(updatedRow, 0), index(updatedRow, 0));
                }
            }
            return false;
        }
    }

    if (containsMessage(message))
    {
        return false;
    }

    // 待确认消息尚无 seq，按发送请求到达模型的顺序放在列表末尾。
    // 服务器确认后会通过 confirmPendingMessage() 按真实 seq 调整位置。
    int insertRow = m_messages.size();
    if (!isPending)
    {
        for (int row = 0; row < m_messages.size(); ++row)
        {
            const ChatMessageRecord &existing = m_messages.at(row);
            if (existing.conversationId == message.conversationId &&
                existing.sequence != 0 && existing.sequence > message.sequence)
            {
                insertRow = row;
                break;
            }
        }
    }

    beginInsertRows(QModelIndex(), insertRow, insertRow);
    m_messages.insert(insertRow, message);
    endInsertRows();
    return true;
}

bool MessageListModel::setSendState(const QString &clientMessageId, MessageSendState state)
{
    if (clientMessageId.isEmpty())
    {
        return false;
    }

    const int row = findMessageByClientMessageId(clientMessageId);
    if (row < 0)
    {
        return false;
    }

    ChatMessageRecord &message = m_messages[row];
    if (message.sendState != state)
    {
        message.sendState = state;
        emit dataChanged(index(row, 0), index(row, 0), {SendStateRole});
    }
    return true;
}

bool MessageListModel::confirmPendingMessage(const QString &clientMessageId,
                                            quint64 serverMessageId,
                                            quint64 sequence)
{
    if (clientMessageId.isEmpty() || serverMessageId == 0 || sequence == 0)
    {
        return false;
    }

    const int row = findMessageByClientMessageId(clientMessageId);
    if (row < 0)
    {
        return false;
    }

    ChatMessageRecord &message = m_messages[row];
    // 防止迟到的确认响应把已经匹配到另一条服务器消息的气泡覆盖掉。
    if (message.serverMessageId != 0 && message.serverMessageId != serverMessageId)
    {
        return false;
    }

    message.serverMessageId = serverMessageId;
    message.sequence = sequence;
    message.sendState = MessageSendState::Sent;

    moveMessageToSequenceOrder(row);
    const int updatedRow = findMessageByClientMessageId(clientMessageId);
    if (updatedRow >= 0)
    {
        emit dataChanged(index(updatedRow, 0), index(updatedRow, 0));
    }
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
        if (message.serverMessageId != 0 &&
            existing.serverMessageId == message.serverMessageId)
        {
            return true;
        }

        // seq=0 只代表本地待确认消息，不能用它来相互去重。
        if (message.sequence != 0 && existing.sequence != 0 &&
            existing.conversationId == message.conversationId &&
            existing.sequence == message.sequence)
        {
            return true;
        }

        if (!message.clientMessageId.isEmpty() &&
            existing.clientMessageId == message.clientMessageId)
        {
            return true;
        }
    }
    return false;
}

int MessageListModel::findMessageByClientMessageId(const QString &clientMessageId) const
{
    if (clientMessageId.isEmpty())
    {
        return -1;
    }

    for (int row = 0; row < m_messages.size(); ++row)
    {
        if (m_messages.at(row).clientMessageId == clientMessageId)
        {
            return row;
        }
    }
    return -1;
}

void MessageListModel::moveMessageToSequenceOrder(int row)
{
    if (row < 0 || row >= m_messages.size())
    {
        return;
    }

    const ChatMessageRecord movingMessage = m_messages.at(row);
    if (movingMessage.sequence == 0)
    {
        return;
    }

    // 在移除当前行后的列表里，找到同一会话中第一个更大的 seq。
    // 若不存在，就把它放到末尾；不同会话之间的相对位置不影响过滤后的展示。
    int destinationRow = m_messages.size() - 1;
    int remainingRow = 0;
    for (int sourceIndex = 0; sourceIndex < m_messages.size(); ++sourceIndex)
    {
        if (sourceIndex == row)
        {
            continue;
        }

        const ChatMessageRecord &other = m_messages.at(sourceIndex);
        if (other.conversationId == movingMessage.conversationId &&
            other.sequence != 0 && other.sequence > movingMessage.sequence)
        {
            destinationRow = remainingRow;
            break;
        }
        ++remainingRow;
    }

    // destinationRow 是移除源行之后的最终索引；Qt 的 destinationChild 使用移除前坐标。
    if (destinationRow == row)
    {
        return;
    }
    const int destinationChild = destinationRow > row ? destinationRow + 1 : destinationRow;
    if (destinationChild == row || destinationChild == row + 1)
    {
        return;
    }

    if (!beginMoveRows(QModelIndex(), row, row, QModelIndex(), destinationChild))
    {
        return;
    }

    const ChatMessageRecord moved = m_messages.takeAt(row);
    m_messages.insert(destinationRow, moved);
    endMoveRows();
}
