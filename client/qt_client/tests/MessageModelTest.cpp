#include "model/ChatMessageRecord.h"
#include "ui/MessageFilterProxyModel.h"
#include "ui/MessageListModel.h"

#include <QCoreApplication>
#include <QDebug>

namespace
{
    bool require(bool condition, const char *message)
    {
        if (!condition)
        {
            qCritical() << "MessageModelTest FAILED:" << message;
            return false;
        }
        return true;
    }

    ChatMessageRecord makeMessage(quint64 conversationId,
                                  quint64 serverMessageId,
                                  const QString &clientMessageId,
                                  quint64 senderId,
                                  const QString &senderName,
                                  quint64 sequence,
                                  const QString &content)
    {
        ChatMessageRecord message;
        message.conversationId = conversationId;
        message.serverMessageId = serverMessageId;
        message.clientMessageId = clientMessageId;
        message.senderId = senderId;
        message.senderName = senderName;
        message.sequence = sequence;
        message.content = content;
        message.timeText = QStringLiteral("09:30");
        return message;
    }
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    MessageListModel model;
    model.setCurrentUsername(QStringLiteral("alice"));

    // 故意先插入 seq=5，再插入 seq=3，验证模型不会只依赖网络到达顺序。
    if (!require(model.appendMessage(makeMessage(
                      501, 1001, QStringLiteral("client-a"), 8,
                      QStringLiteral("bob"), 5, QStringLiteral("第二条消息"))),
                  "insert first message"))
    {
        return 1;
    }
    if (!require(model.appendMessage(makeMessage(
                      501, 1002, QStringLiteral("client-b"), 7,
                      QStringLiteral("alice"), 3, QStringLiteral("第一条消息"))),
                  "insert out-of-order message"))
    {
        return 1;
    }
    if (!require(model.rowCount() == 2, "message count after insertion"))
    {
        return 1;
    }
    if (!require(model.index(0, 0).data(MessageListModel::SequenceRole).toULongLong() == 3,
                  "lower sequence must appear first"))
    {
        return 1;
    }
    if (!require(model.index(1, 0).data(MessageListModel::SequenceRole).toULongLong() == 5,
                  "higher sequence must appear second"))
    {
        return 1;
    }
    if (!require(model.index(0, 0).data(MessageListModel::IsSelfRole).toBool(),
                  "current account message must be marked as self"))
    {
        return 1;
    }
    if (!require(!model.index(1, 0).data(MessageListModel::IsSelfRole).toBool(),
                  "other account message must not be marked as self"))
    {
        return 1;
    }

    if (!require(!model.appendMessage(makeMessage(
                      501, 1002, QStringLiteral("another-client-id"), 7,
                      QStringLiteral("alice"), 6, QStringLiteral("duplicate server id"))),
                  "duplicate server_msg_id must be ignored"))
    {
        return 1;
    }
    if (!require(!model.appendMessage(makeMessage(
                      501, 1003, QStringLiteral("client-b"), 7,
                      QStringLiteral("alice"), 7, QStringLiteral("duplicate client id"))),
                  "duplicate client_msg_id must be ignored"))
    {
        return 1;
    }

    // 历史查询接口当前没有返回 server_msg_id；同会话 seq 应作为历史/实时合并的去重键。
    if (!require(model.appendMessage(makeMessage(
                      501, 0, QString(), 8, QStringLiteral("bob"), 2,
                      QStringLiteral("来自历史接口的消息"))),
                  "historical message without server id should be accepted"))
    {
        return 1;
    }
    if (!require(!model.appendMessage(makeMessage(
                      501, 1009, QStringLiteral("live-copy"), 8, QStringLiteral("bob"), 2,
                      QStringLiteral("同一条消息的实时推送"))),
                  "history and live push with same conversation seq must deduplicate"))
    {
        return 1;
    }

    MessageFilterProxyModel proxy;
    proxy.setSourceModel(&model);
    proxy.setConversationId(501);
    if (!require(proxy.rowCount() == 3, "proxy should show only selected conversation"))
    {
        return 1;
    }

    if (!require(model.appendMessage(makeMessage(
                      502, 1004, QStringLiteral("client-c"), 9,
                      QStringLiteral("carol"), 1, QStringLiteral("另一会话消息"))),
                  "insert another conversation message"))
    {
        return 1;
    }
    if (!require(proxy.rowCount() == 3, "another conversation must not leak into current view"))
    {
        return 1;
    }
    proxy.setConversationId(502);
    if (!require(proxy.rowCount() == 1, "switching conversation should change filtered rows"))
    {
        return 1;
    }
    proxy.setConversationId(0);
    if (!require(proxy.rowCount() == 0, "demo conversation ID zero must show no network messages"))
    {
        return 1;
    }

    model.clearMessages();
    if (!require(model.rowCount() == 0 && proxy.rowCount() == 0,
                  "clearing messages must also clear proxy view"))
    {
        return 1;
    }

    if (!require(model.appendMessage(makeMessage(
                      501, 1005, QStringLiteral("client-after-clear"), 7,
                      QStringLiteral("alice"), 1, QStringLiteral("清理后新账号消息"))),
                  "insert after clear"))
    {
        return 1;
    }
    if (!require(!model.index(0, 0).data(MessageListModel::IsSelfRole).toBool(),
                  "clearMessages must clear old logged-in username"))
    {
        return 1;
    }

    qInfo() << "MessageModelTest passed";
    return 0;
}
