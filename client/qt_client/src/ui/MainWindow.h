#pragma once

#include <QMainWindow>
#include <QPoint>
#include <QRect>
#include <Qt>
#include <QtGlobal>
#include <QString>
#include <QVector>
#include "ui/SessionListPanel.h"
#include "model/ChatMessageRecord.h"

class QEvent;
class QMouseEvent;
class QPaintEvent;
class QResizeEvent;
class QStackedWidget;
class QWidget;
class ChatArea;

// 客户端主窗口：展示三栏聊天工作台，并统一矩形窗口与窗口控制按钮。
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

    // 在输入区显示发送状态，例如服务器确认保存或返回失败原因。
    void setMessageStatus(const QString &message, bool isError = false);
    // 更新主窗口中的 TCP 连接状态徽标。
    void setNetworkStatus(const QString &message, const QString &state);
    // TCP 层接受消息发送请求后，清空编辑框中的已发送文本。
    void clearMessageInput();
    // 设置当前登录用户名，以便气泡正确区分自己和对方的消息。
    void setCurrentUsername(const QString &username);
    // 将服务端推送的消息交给聊天区模型；返回 false 表示重复或无效消息。
    bool appendMessage(const ChatMessageRecord &message);
    bool setMessageSendState(const QString &clientMessageId, MessageSendState state);
    bool confirmPendingMessage(const QString &clientMessageId,
                              quint64 serverMessageId, quint64 sequence);
    // 退出登录、断线或会话状态失效时清理消息内存。
    void clearMessages();
    void setHistoryLoading(quint64 conversationId, bool loading, bool hasMore = true);
    // 用服务端返回的数据替换左侧会话列表；preferredConversationId 用于新建会话后自动打开它。
    void setSessions(const QVector<SessionItem> &sessions, quint64 preferredConversationId = 0);
    void clearSessions();

signals:
    // 用户从侧边栏“更多”菜单中选择退出登录时发出。
    void logoutRequested();
    void newSessionRequested();

    // 输入区请求发送文本；会话 ID 与类型来自当前选中的会话。
    void messageSendRequested(const QString &text, quint64 conversationId, bool isGroup);
    void historyRequested(quint64 conversationId);
    void messageRetryRequested(const QString &clientMessageId);

protected:
    // 绘制铺满窗口的底色与紫色光晕。
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    // 无系统标题栏时，允许用户拖动窗口。
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    // 页面空白区域的鼠标事件也交由主窗口处理，同时监控无边框窗口的缩放边缘。
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    // 将 SideBar、SessionListPanel、ChatArea 组合为完整聊天工作台。
    QWidget *createChatWorkspacePage();
    // 给页面及其子控件安装事件过滤器，使鼠标拖到窗口边缘时也能缩放。
    void installResizeEventFilters(QWidget *widget);
    // 根据全局鼠标位置判断当前位于哪个缩放边缘。
    Qt::Edges resizeEdgesAt(const QPoint &globalPosition) const;
    // 根据边缘方向设置合适的鼠标指针。
    void updateResizeCursor(QWidget *watched, const QPoint &globalPosition);

    QStackedWidget *m_pageStack = nullptr; // 页面栈，只显示当前页面。
    QWidget *m_windowControls = nullptr;   // 右上角与背景融合的圆点控制按钮。
    QPoint m_dragOffset;              // 鼠标按下位置相对窗口左上角的偏移量。
    bool m_dragging = false;          // 当前是否正在拖动窗口。
    bool m_resizing = false;          // 当前是否正在拖动窗口边缘缩放。
    Qt::Edges m_resizeEdges;          // 当前正在拖动的边缘/角。
    QPoint m_resizeStartGlobal;       // 缩放开始时的全局鼠标坐标。
    QRect m_resizeStartGeometry;      // 缩放开始时的窗口几何区域。

    ChatArea *m_chatArea = nullptr;    // 当前聊天区，负责显示发送状态。
    SessionListPanel *m_sessionPanel = nullptr; // 中间栏的真实会话列表。
    quint64 m_currentConversationId = 0; // 当前数据库会话 ID；0 表示尚未选中真实会话。
    bool m_currentConversationIsGroup = false; // 当前会话是否为群聊。
};
