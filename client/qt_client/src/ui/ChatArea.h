#pragma once

#include <QWidget>
#include <QString>
#include <QtGlobal>

#include "model/ChatMessageRecord.h"

class QLabel;
class QListView;
class QSplitter;
class QResizeEvent;
class InputBar;
class MessageListModel;
class MessageFilterProxyModel;
class MessageItemDelegate;

// 聊天主区域，对应最终 HTML 设计稿中的 .chat-panel。
// 包含会话 Header、消息列表占位区和底部输入区；顶层窗口控制由 MainWindow 统一管理。
class ChatArea : public QWidget
{
    Q_OBJECT

public:
    explicit ChatArea(QWidget *parent = nullptr);

    // 更新当前会话标题、头像文字和状态文案（例如“私聊”“群聊”）。
    // 当前服务端还未提供对方在线状态，因此不虚构对方在线/离线。
    void setConversationInfo(const QString &name,
                             const QString &avatarText,
                             const QString &statusText);
    // 显示当前客户端与服务器的连接状态；和会话类型状态彼此独立。
    void setNetworkStatus(const QString &message, const QString &state);

    // 提供消息列表和输入区访问接口，方便后续接入模型与控制器。
    QListView *messageListView() const;
    InputBar *inputBar() const;

    // 将网络发送状态显示在输入区，不在聊天页面上弹模态窗口。
    void setMessageStatus(const QString &message, bool isError = false);
    // 控制器在 TCP 层接受发送请求后清空编辑框。
    void clearMessageInput();

    // 切换当前会话时只显示该会话的消息；ID 为 0 表示当前没有选中真实会话。
    void setCurrentConversationId(quint64 conversationId);
    // 用登录成功的用户名区分自己发送的气泡和对方发送的气泡。
    void setCurrentUsername(const QString &username);
    // 将服务器确认保存的消息写入模型；相同 server_msg_id 会被自动去重。
    bool appendMessage(const ChatMessageRecord &message);
    bool setMessageSendState(const QString &clientMessageId, MessageSendState state);
    bool confirmPendingMessage(const QString &clientMessageId,
                              quint64 serverMessageId, quint64 sequence);
    // 清除上一登录会话的内存消息。
    void clearMessages();
    void setHistoryLoading(bool loading, bool hasMore = true);

signals:
    // 输入区发送文本时向外转发；后续由控制器连接并提交到网络层。
    void sendRequested(const QString &text);
    void historyRequested(quint64 conversationId);
    void retryMessageRequested(const QString &clientMessageId);

protected:
    // 窗口尺寸变化时，更新输入区允许的最大高度，确保它不超过消息区。
    void resizeEvent(QResizeEvent *event) override;

private:
    QLabel *m_avatarLabel = nullptr;
    QLabel *m_nameLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_statusDot = nullptr;
    QWidget *m_networkStatusBadge = nullptr;
    QLabel *m_networkStatusLabel = nullptr;
    QLabel *m_networkStatusDot = nullptr;
    QSplitter *m_contentSplitter = nullptr; // 垂直分隔消息区和输入区，允许用户调整比例。
    QListView *m_messageListView = nullptr;
    InputBar *m_inputBar = nullptr;

    MessageListModel *m_messageModel = nullptr;
    MessageFilterProxyModel *m_messageFilterModel = nullptr;
    MessageItemDelegate *m_messageDelegate = nullptr;
    quint64 m_currentConversationId = 0;
};
