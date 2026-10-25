#pragma once

#include <QPixmap>
#include <QString>
#include <QWidget>

// 单条聊天消息气泡。
// 使用 QPainter 自绘，不依赖嵌套 QSS；可通过 sizeHint() 提供建议尺寸。
class MessageBubble : public QWidget
{
    Q_OBJECT

public:
    explicit MessageBubble(QWidget *parent = nullptr);

    // 设置消息方向、文本、时间和头像。
    // isSelf 为 true 表示当前用户发送的消息。
    void setMessage(bool isSelf,
                    const QString &text,
                    const QString &time,
                    const QPixmap &avatar);

    // 返回该消息在默认最大气泡宽度下的建议尺寸。
    QSize sizeHint() const override;

protected:
    // 绘制头像、时间、圆角气泡和消息文本。
    void paintEvent(QPaintEvent *event) override;

private:
    // 按指定文本宽度计算换行后的文本总高度。
    int textHeightForWidth(int textWidth) const;

    // 当前消息是否由自己发送。
    bool m_isSelf = false;

    // 消息正文和显示时间。
    QString m_text;
    QString m_time;

    // 头像图片；为空时绘制默认圆形占位头像。
    QPixmap m_avatar;
};
