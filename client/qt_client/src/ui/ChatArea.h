#pragma once

#include <QWidget>
#include <QString>

class QLabel;
class QListView;
class QToolButton;
class InputBar;

// 聊天主区域，对应最终 HTML 设计稿中的 .chat-panel。
// 包含会话 Header、消息列表占位区、底部输入区和无边框窗口控制按钮。
class ChatArea : public QWidget
{
    Q_OBJECT

public:
    explicit ChatArea(QWidget *parent = nullptr);

    // 更新当前会话的标题、头像文字和在线状态。
    void setConversationInfo(const QString &name,
                             const QString &avatarText,
                             bool online);

    // 提供消息列表和输入区访问接口，方便后续接入模型与控制器。
    QListView *messageListView() const;
    InputBar *inputBar() const;

signals:
    // 输入区发送文本时向外转发；后续由控制器连接并提交到网络层。
    void sendRequested(const QString &text);

private:
    // 窗口控制按钮操作顶层窗口，而不是只隐藏 ChatArea 组件。
    void minimizeWindow();
    void toggleMaximizeWindow();
    void closeWindow();

    QLabel *m_avatarLabel = nullptr;
    QLabel *m_nameLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_statusDot = nullptr;
    QListView *m_messageListView = nullptr;
    InputBar *m_inputBar = nullptr;
    QToolButton *m_minimizeButton = nullptr;
    QToolButton *m_maximizeButton = nullptr;
    QToolButton *m_closeButton = nullptr;
};
