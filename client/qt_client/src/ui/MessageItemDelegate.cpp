#include "MessageItemDelegate.h"

#include "MessageListModel.h"

#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include <QBrush>
#include <QWidget>
#include <QLinearGradient>
#include <QPen>
#include <QStringList>
#include <QTextOption>
#include <QtMath>

namespace
{
    constexpr int kOuterMargin = 6;
    constexpr int kAvatarSize = 34;
    constexpr int kAvatarGap = 9;
    constexpr int kBubblePaddingH = 15;
    constexpr int kBubblePaddingV = 9;
    constexpr int kBubbleRadius = 17;
    constexpr int kMetaHeight = 13;
    constexpr int kMetaGap = 4;
    constexpr int kRowGap = 7;
    constexpr int kMaximumBubbleWidth = 480;

    QFont messageFont()
    {
        QFont font(QStringLiteral("Microsoft YaHei"));
        font.setPixelSize(12);
        return font;
    }

    QFont metaFont()
    {
        QFont font(QStringLiteral("Microsoft YaHei"));
        font.setPixelSize(9);
        return font;
    }

    int bubbleMaximumWidth(int rowWidth)
    {
        // 气泡最多占用可用空间的大约 70%，同时保持一个合理的上限。
        const int room = qMax(120, rowWidth - 2 * kOuterMargin - kAvatarSize - kAvatarGap);
        return qMax(120, qMin(kMaximumBubbleWidth, room * 7 / 10));
    }

    int textHeightFor(const QString &text, int textWidth)
    {
        const QFontMetrics metrics(messageFont());
        const QRect bounds = metrics.boundingRect(
            QRect(0, 0, qMax(1, textWidth), 100000),
            Qt::TextWordWrap | Qt::TextWrapAnywhere,
            text);
        return qMax(metrics.height(), bounds.height());
    }

    int bubbleWidthFor(const QString &text, int maxBubbleWidth)
    {
        const QFontMetrics metrics(messageFont());
        int widestLine = 0;
        const QStringList lines = text.split(QLatin1Char('\n'));
        for (const QString &line : lines)
        {
            widestLine = qMax(widestLine, metrics.horizontalAdvance(line));
        }

        return qMin(maxBubbleWidth,
                    qMax(64, widestLine + 2 * kBubblePaddingH));
    }
}

MessageItemDelegate::MessageItemDelegate(QObject *parent)
    : QStyledItemDelegate(parent)
{
}

QSize MessageItemDelegate::sizeHint(const QStyleOptionViewItem &option,
                                    const QModelIndex &index) const
{
    int rowWidth = option.rect.width();
    if (option.widget != nullptr && option.widget->width() > 0)
    {
        rowWidth = option.widget->width();
    }
    if (rowWidth < 180)
    {
        rowWidth = 720;
    }

    const QString text = index.data(MessageListModel::ContentRole).toString();
    const int maxBubbleWidth = bubbleMaximumWidth(rowWidth);
    const int bubbleWidth = bubbleWidthFor(text, maxBubbleWidth);
    const int textWidth = qMax(1, bubbleWidth - 2 * kBubblePaddingH);
    const int textHeight = textHeightFor(text, textWidth);
    const int bubbleHeight = textHeight + 2 * kBubblePaddingV;

    const int rowHeight = 2 * kOuterMargin + kMetaHeight + kMetaGap + bubbleHeight;
    return QSize(rowWidth, rowHeight + kRowGap);
}

void MessageItemDelegate::paint(QPainter *painter,
                                const QStyleOptionViewItem &option,
                                const QModelIndex &index) const
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setRenderHint(QPainter::TextAntialiasing, true);

    const QRectF rowRect = option.rect;
    const bool isSelf = index.data(MessageListModel::IsSelfRole).toBool();
    const QString text = index.data(MessageListModel::ContentRole).toString();
    const QString senderName = index.data(MessageListModel::SenderNameRole).toString();
    const QString timeText = index.data(MessageListModel::TimeTextRole).toString();
    const QString metaText = (!isSelf && !senderName.isEmpty())
                                 ? senderName + QStringLiteral("  ") + timeText
                                 : timeText;

    const int maxBubbleWidth = bubbleMaximumWidth(qRound(rowRect.width()));
    const int bubbleWidth = bubbleWidthFor(text, maxBubbleWidth);
    const int textWidth = qMax(1, bubbleWidth - 2 * kBubblePaddingH);
    const int textHeight = textHeightFor(text, textWidth);
    const int bubbleHeight = textHeight + 2 * kBubblePaddingV;

    const qreal avatarY = rowRect.top() + kOuterMargin + kMetaHeight + kMetaGap;
    const qreal avatarX = isSelf
                              ? rowRect.right() - kOuterMargin - kAvatarSize
                              : rowRect.left() + kOuterMargin;
    const qreal bubbleX = isSelf
                              ? avatarX - kAvatarGap - bubbleWidth
                              : avatarX + kAvatarSize + kAvatarGap;
    const qreal bubbleY = avatarY;
    const QRectF avatarRect(avatarX, avatarY, kAvatarSize, kAvatarSize);
    const QRectF bubbleRect(bubbleX, bubbleY, bubbleWidth, bubbleHeight);

    // 元信息放在消息气泡上方；自己的消息靠右，对方消息靠左。
    painter->setFont(metaFont());
    painter->setPen(QColor(163, 155, 185));
    const QFontMetrics metaMetrics(metaFont());
    const qreal metaWidth = metaMetrics.horizontalAdvance(metaText);
    const qreal metaX = isSelf ? bubbleRect.right() - metaWidth : bubbleRect.left();
    painter->drawText(QPointF(metaX, rowRect.top() + kOuterMargin + metaMetrics.ascent()), metaText);

    // 头像采用渐变圆形占位，文字取发送者名称首字。
    QPainterPath avatarPath;
    avatarPath.addEllipse(avatarRect);
    painter->save();
    painter->setClipPath(avatarPath);
    QLinearGradient avatarGradient(avatarRect.topLeft(), avatarRect.bottomRight());
    if (isSelf)
    {
        avatarGradient.setColorAt(0, QColor(151, 133, 248));
        avatarGradient.setColorAt(1, QColor(91, 75, 214));
    }
    else
    {
        avatarGradient.setColorAt(0, QColor(232, 224, 255));
        avatarGradient.setColorAt(1, QColor(182, 168, 231));
    }
    painter->fillRect(avatarRect, QBrush(avatarGradient));
    painter->setFont(QFont(QStringLiteral("Microsoft YaHei"), 10, QFont::DemiBold));
    painter->setPen(isSelf ? Qt::white : QColor(85, 68, 135));
    painter->drawText(avatarRect, Qt::AlignCenter,
                      senderName.isEmpty() ? QStringLiteral("?") : senderName.left(1));
    painter->restore();

    // 对方消息是白色气泡；自己的消息使用紫色渐变。
    if (isSelf)
    {
        QLinearGradient bubbleGradient(bubbleRect.topLeft(), bubbleRect.bottomRight());
        bubbleGradient.setColorAt(0, QColor(143, 123, 248));
        bubbleGradient.setColorAt(1, QColor(101, 79, 235));
        painter->setPen(Qt::NoPen);
        painter->setBrush(QBrush(bubbleGradient));
        painter->drawRoundedRect(bubbleRect, kBubbleRadius, kBubbleRadius);
        painter->setPen(Qt::white);
    }
    else
    {
        painter->setPen(QPen(QColor(104, 84, 162, 12), 1));
        painter->setBrush(QColor(255, 255, 255));
        painter->drawRoundedRect(bubbleRect, kBubbleRadius, kBubbleRadius);
        painter->setPen(QColor(76, 69, 102));
    }

    painter->setFont(messageFont());
    const QRectF textRect = bubbleRect.adjusted(
        kBubblePaddingH, kBubblePaddingV,
        -kBubblePaddingH, -kBubblePaddingV);
    painter->drawText(textRect,
                      Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap | Qt::TextWrapAnywhere,
                      text);

    painter->restore();
}
