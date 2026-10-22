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

signals:
    // 点击“发送”后发出；聊天区域或控制器可以连接此信号处理消息。
    void sendRequested(const QString &text);

private slots:
    // 读取编辑框内容，去掉首尾空白后发送非空文本。
    void handleSendClicked();

protected:
    // Enter 发送消息；Shift+Enter 保留 QTextEdit 默认行为用于换行。
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QTextEdit *m_editor = nullptr;       // 多行消息编辑框。
    QPushButton *m_sendButton = nullptr; // 渐变发送按钮。
};
