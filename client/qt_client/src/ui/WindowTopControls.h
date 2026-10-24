#pragma once

#include <QHBoxLayout>
#include <QPushButton>
#include <QWidget>

// 三个同色系的圆点窗口控制按钮。按钮本身透明，因此会自然融入所在页面背景。
// parent 是按钮所在页面，targetWindow 是需要执行窗口操作的顶层窗口。
inline QWidget *createWindowTopControls(QWidget *parent, QWidget *targetWindow = nullptr)
{
    if (parent == nullptr)
    {
        return nullptr;
    }

    if (targetWindow == nullptr)
    {
        targetWindow = parent->window();
    }

    auto *container = new QWidget(parent);
    container->setObjectName(QStringLiteral("windowTopControls"));
    container->setAttribute(Qt::WA_StyledBackground, false);
    container->setAutoFillBackground(false);

    auto *layout = new QHBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    const auto createDot = [container, layout](const QString &objectName,
                                                const QString &symbol,
                                                const QString &tooltip)
    {
        auto *button = new QPushButton(symbol, container);
        button->setObjectName(objectName);
        button->setFixedSize(12, 12);
        button->setCursor(Qt::PointingHandCursor);
        button->setFocusPolicy(Qt::NoFocus);
        button->setToolTip(tooltip);
        // 平时是透明的空心圆点；悬停时显示柔和的紫灰底色和操作符号。
        button->setStyleSheet(QStringLiteral(
            "QPushButton#%1 {"
            " background-color: transparent;"
            " border: 1px solid rgba(106, 83, 246, 55);"
            " border-radius: 6px; color: transparent; padding: 0;"
            " font-size: 8px; font-weight: 800;"
            " }"
            "QPushButton#%1:hover {"
            " background-color: rgba(106, 83, 246, 22);"
            " border-color: rgba(106, 83, 246, 100); color: #6A53F6;"
            " }"
            "QPushButton#%1:pressed {"
            " background-color: rgba(106, 83, 246, 42);"
            " border-color: rgba(106, 83, 246, 125); color: #5140C8;"
            " }").arg(objectName));
        layout->addWidget(button);
        return button;
    };

    auto *closeButton = createDot(QStringLiteral("windowCloseDot"),
                                  QStringLiteral("×"), QStringLiteral("关闭"));
    auto *minimizeButton = createDot(QStringLiteral("windowMinimizeDot"),
                                     QStringLiteral("−"), QStringLiteral("最小化"));
    auto *maximizeButton = createDot(QStringLiteral("windowMaximizeDot"),
                                     QStringLiteral("+"), QStringLiteral("最大化/还原"));

    QObject::connect(closeButton, &QPushButton::clicked, targetWindow,
                     [targetWindow]() { targetWindow->close(); });
    QObject::connect(minimizeButton, &QPushButton::clicked, targetWindow,
                     [targetWindow]() { targetWindow->showMinimized(); });
    QObject::connect(maximizeButton, &QPushButton::clicked, targetWindow,
                     [targetWindow]()
    {
        if (targetWindow->isMaximized() || targetWindow->isFullScreen())
        {
            targetWindow->showNormal();
        }
        else
        {
            targetWindow->showMaximized();
        }
    });

    container->setFixedSize(layout->sizeHint());
    return container;
}
