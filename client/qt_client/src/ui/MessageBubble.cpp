#include "MessageBubble.h"

#include <QFont>
#include <QFontMetrics>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTextLayout>
#include <QTextOption>
#include <QVector>
#include <QtMath>

namespace
{
    // 对应 HTML 的 .message-row / .bubble 布局参数。
    constexpr int kAvatarSize = 40;       // HTML：头像 40px
    constexpr int kRowGap = 10;           // HTML：头像与消息主体间距 10px
    constexpr int kOuterMargin = 4;       // 留出少量空间，避免绘制贴边
    constexpr int kMaxBubbleWidth = 480;  // 限制长消息宽度
    constexpr int kBubblePaddingH = 16;   // 气泡左右内边距
    constexpr int kBubblePaddingV = 10;   // 气泡上下内边距
    constexpr int kBubbleRadius = 24;     // 胶囊风格圆角
    constexpr int kMetaHeight = 12;       // 时间文字区域高度
    constexpr int kMetaBubbleGap = 5;     // 时间与气泡之间的间距
    constexpr int kLineHeight = 21;       // HTML 12px 字体、1.75 行高约为 21px

    const QColor kIncomingText(76, 69, 102);     // #4C4566
    const QColor kMetaText(163, 155, 185);       // #A39BB9
    const QColor kIncomingBorder(104, 84, 162, 9); // rgba(104, 84, 162, .035)
    const QColor kIncomingShadow(93, 70, 150, 19); // 柔和的来信气泡阴影

    QFont makeMessageFont()
    {
        QFont font(QStringLiteral("Microsoft YaHei"));
        font.setPixelSize(12); // 对应 HTML .bubble 的 12px
        return font;
    }

    QFont makeMetaFont()
    {
        QFont font(QStringLiteral("Microsoft YaHei"));
        font.setPixelSize(9); // 对应 HTML .message-meta 的 9px
        return font;
    }

    // 创建 QTextLayout，并按固定行高排列文本，供测量和绘制共用。
    int createTextLayout(QTextLayout &layout, int textWidth)
    {
        QTextOption option;
        // 优先按单词边界换行；遇到超长内容时也允许在任意位置换行。
        option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        layout.setTextOption(option);

        int y = 0;
        layout.beginLayout();

        while (true)
        {
            QTextLine line = layout.createLine();
            if (!line.isValid())
            {
                break;
            }

            line.setLineWidth(qMax(1, textWidth));
            line.setPosition(QPointF(0, y));
            y += kLineHeight;
        }

        layout.endLayout();
        return qMax(kLineHeight, y);
    }
}

MessageBubble::MessageBubble(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("messageBubble"));
    setAttribute(Qt::WA_TranslucentBackground, true);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
}

void MessageBubble::setMessage(bool isSelf,
                               const QString &text,
                               const QString &time,
                               const QPixmap &avatar)
{
    m_isSelf = isSelf;
    m_text = text;
    m_time = time;
    m_avatar = avatar;

    // 内容变化后，建议尺寸和绘制结果都需要重新计算。
    updateGeometry();
    update();
}

QSize MessageBubble::sizeHint() const
{
    const int maxBubbleWidth = kMaxBubbleWidth;
    const int textWidth = maxBubbleWidth - 2 * kBubblePaddingH;

    QTextLayout layout(m_text, makeMessageFont());
    const int textHeight = createTextLayout(layout, textWidth);
    const int bubbleHeight = textHeight + 2 * kBubblePaddingV;

    // 一行时间文字 + 间距 + 气泡，再在上下各留少量空间。
    const int totalHeight = 2 * kOuterMargin
                            + kMetaHeight
                            + kMetaBubbleGap
                            + bubbleHeight;

    // 头像、头像间距和最大气泡宽度共同决定建议宽度。
    const int totalWidth = 2 * kOuterMargin
                           + kAvatarSize
                           + kRowGap
                           + maxBubbleWidth;

    return QSize(totalWidth, totalHeight);
}

int MessageBubble::textHeightForWidth(int textWidth) const
{
    QTextLayout layout(m_text, makeMessageFont());
    return createTextLayout(layout, textWidth);
}

void MessageBubble::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    const QRectF bounds = rect();
    const qreal outerMargin = kOuterMargin;
    const qreal avatarSize = kAvatarSize;
    const qreal rowGap = kRowGap;

    // 根据当前控件宽度计算气泡可用宽度，避免窄窗口下超出边界。
    const qreal availableBubbleWidth = qMax(
        1.0,
        bounds.width() - 2.0 * outerMargin - avatarSize - rowGap);

    const int maxBubbleWidth = qMax(
        1,
        qMin(kMaxBubbleWidth, qFloor(availableBubbleWidth)));

    const int textWidth = qMax(1, maxBubbleWidth - 2 * kBubblePaddingH);

    // 先进行文本排版，确保气泡高度与实际换行结果一致。
    QTextLayout textLayout(m_text, makeMessageFont());
    const int textHeight = createTextLayout(textLayout, textWidth);

    const qreal bubbleHeight = textHeight + 2 * kBubblePaddingV;
    const qreal metaTop = outerMargin;
    const qreal bubbleTop = metaTop + kMetaHeight + kMetaBubbleGap;

    // 计算每一行的实际宽度，让短消息气泡不会被强制撑到最大宽度。
    qreal widestLine = 0;
    for (int i = 0; i < textLayout.lineCount(); ++i)
    {
        widestLine = qMax(widestLine,
                          textLayout.lineAt(i).naturalTextWidth());
    }

    const qreal bubbleWidth = qMin(
        qreal(maxBubbleWidth),
        qMax(qreal(2 * kBubblePaddingH + 1),
             widestLine + 2 * kBubblePaddingH));

    const qreal avatarY = outerMargin;
    qreal avatarX = outerMargin;
    qreal bubbleX = outerMargin + avatarSize + rowGap;

    if (m_isSelf)
    {
        // 自己发送的消息整体靠右，头像放在最右侧。
        avatarX = bounds.width() - outerMargin - avatarSize;
        bubbleX = avatarX - rowGap - bubbleWidth;
    }

    const QRectF avatarRect(avatarX, avatarY, avatarSize, avatarSize);
    const QRectF bubbleRect(bubbleX, bubbleTop, bubbleWidth, bubbleHeight);

    // 时间文字放在气泡上方；自己发送的消息靠右，对方消息靠左。
    painter.setFont(makeMetaFont());
    painter.setPen(kMetaText);

    const QString displayTime = m_time;
    const QFontMetrics metaMetrics(makeMetaFont());
    const qreal timeWidth = metaMetrics.horizontalAdvance(displayTime);
    const qreal timeX = m_isSelf
                            ? bubbleRect.right() - timeWidth
                            : bubbleRect.left();

    painter.drawText(QPointF(timeX, metaTop + metaMetrics.ascent()),
                     displayTime);

    // 头像剪裁成圆形；未提供图片时绘制柔和的占位圆。
    QPainterPath avatarPath;
    avatarPath.addEllipse(avatarRect);

    painter.save();
    painter.setClipPath(avatarPath);

    if (!m_avatar.isNull())
    {
        painter.drawPixmap(avatarRect.toRect(),
                           m_avatar,
                           m_avatar.rect());
    }
    else
    {
        painter.fillRect(avatarRect, QColor(236, 230, 255)); // #ECE6FF
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(106, 83, 246)); // #6A53F6
        const QRectF headRect(avatarRect.center().x() - 5,
                              avatarRect.top() + 8,
                              10,
                              10);
        const QRectF bodyRect(avatarRect.center().x() - 10,
                              avatarRect.top() + 21,
                              20,
                              13);
        painter.drawEllipse(headRect);
        painter.drawRoundedRect(bodyRect, 7, 7);
    }

    painter.restore();

    // 对方的白色气泡加极轻微柔和阴影；自己发送的气泡使用紫色渐变。
    if (!m_isSelf)
    {
        // 通过两层半透明圆角矩形模拟轻微阴影，避免额外依赖图形效果类。
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(kIncomingShadow.red(),
                                kIncomingShadow.green(),
                                kIncomingShadow.blue(),
                                10));
        painter.drawRoundedRect(bubbleRect.translated(0, 2),
                                kBubbleRadius,
                                kBubbleRadius);

        painter.setBrush(QColor(kIncomingShadow.red(),
                                kIncomingShadow.green(),
                                kIncomingShadow.blue(),
                                7));
        painter.drawRoundedRect(bubbleRect.translated(0, 1),
                                kBubbleRadius,
                                kBubbleRadius);

        painter.setBrush(Qt::white);
        painter.setPen(QPen(kIncomingBorder, 1));
        painter.drawRoundedRect(bubbleRect,
                                kBubbleRadius,
                                kBubbleRadius);
    }
    else
    {
        // 对应 HTML --brand-gradient：#806BFF → #6A53F6 → #927BFF。
        QLinearGradient gradient(bubbleRect.topLeft(),
                                 bubbleRect.bottomRight());
        gradient.setColorAt(0.0, QColor(128, 107, 255)); // #806BFF
        gradient.setColorAt(0.52, QColor(106, 83, 246)); // #6A53F6
        gradient.setColorAt(1.0, QColor(146, 123, 255)); // #927BFF

        painter.setPen(Qt::NoPen);
        painter.setBrush(gradient);
        painter.drawRoundedRect(bubbleRect,
                                kBubbleRadius,
                                kBubbleRadius);
    }

    // 在气泡内绘制文本。QTextLayout 与测量阶段使用同样的换行规则。
    painter.save();
    painter.setFont(makeMessageFont());
    painter.setPen(m_isSelf ? Qt::white : kIncomingText);

    const QPointF textOrigin(bubbleRect.left() + kBubblePaddingH,
                             bubbleRect.top() + kBubblePaddingV);
    painter.translate(textOrigin);

    for (int i = 0; i < textLayout.lineCount(); ++i)
    {
        const QTextLine line = textLayout.lineAt(i);
        line.draw(&painter, QPointF(0, 0));
    }

    painter.restore();
}
