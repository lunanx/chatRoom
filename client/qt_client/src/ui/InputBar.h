#pragma once

#include <QWidget>
#include <QString>

class QLabel;
class QPushButton;
class QTextEdit;
class QToolButton;

// 聊天输入区：工具栏、消息编辑框和发送按钮。
// 当前只负责收集并发送用户输入的文本，表情、图片等工具后续再接入实际功能。
class InputBar : public QWidget
{
    Q_OBJECT

public:
    explicit InputBar(QWidget *parent = nullptr);

    // 方便会话切换或发送成功后清空输入内容。
    void clearInput();
    // 返回当前编辑框中的文本。
    QString inputText() const;

    // 更新历史消息按钮状态；是否存在真实会话由 ChatArea 传入。
    void setHistoryLoading(bool loading, bool hasMore = true, bool hasConversation = true);
    // 在输入区显示发送结果或错误，不使用模态弹窗。
    void setStatusMessage(const QString &message, bool isError = false);

signals:
    // 点击“发送”后发出；聊天区域或控制器可以连接此信号处理消息。
    void sendRequested(const QString &text);
    // 用户点击工具栏右侧的“加载历史消息”按钮时发出。
    void historyRequested();

private slots:
    // 读取编辑框内容，去掉首尾空白后发送非空文本。
    void handleSendClicked();

protected:
    // Enter 发送消息；Shift+Enter 保留 QTextEdit 默认行为用于换行。
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QTextEdit *m_editor = nullptr;        // 多行消息编辑框。
    QPushButton *m_sendButton = nullptr;  // 渐变发送按钮。
    QPushButton *m_historyButton = nullptr; // 位于工具栏右侧的历史消息按钮。
    QLabel *m_statusLabel = nullptr;      // 非模态发送状态提示，默认隐藏。
};
