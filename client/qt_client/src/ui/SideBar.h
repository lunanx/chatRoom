#pragma once

#include <QWidget>
#include <QString>

class QButtonGroup;
class QLabel;
class QToolButton;

// 左侧主导航栏，对应最终 HTML 设计稿中的 .nav-rail。
// 这里只负责导航栏的外观与选中状态；具体页面切换由父窗口接收信号后处理。
class SideBar : public QWidget
{
    Q_OBJECT

public:
    // 导航项目的固定顺序；信号会把对应项目的整数索引传给父窗口。
    enum Section
    {
        Messages = 0, // 消息
        Contacts,     // 联系人
        Favorites,    // 收藏
        Files         // 文件
    };
    Q_ENUM(Section)

    explicit SideBar(QWidget *parent = nullptr);

    // 设置当前选中的导航项目，index 范围为 Messages 到 Files。
    void setCurrentSection(int index);
    // 返回当前选中项目的索引；初始值为 Messages（0）。
    int currentSection() const;

signals:
    // 用户点击消息、联系人、收藏或文件时发出，由主窗口负责切换右侧内容。
    void sectionChanged(int index);

    // 从“更多”菜单选择退出登录时发出，由主窗口转发给 AppController。
    void logoutRequested();

private:
    // 创建一个统一尺寸的导航按钮，避免四个主导航和底部按钮样式不一致。
    QToolButton *createNavButton(const QString &text,
                                 const QString &toolTip,
                                 const QString &objectName,
                                 QWidget *parent = nullptr);
    // 根据按钮的 checked 状态刷新 active 属性，让 QSS 重新应用对应样式。
    void refreshButtonStyle(QToolButton *button);

    QButtonGroup *m_mainButtonGroup = nullptr; // 保证四个主导航按钮同一时间只有一个选中。
    QLabel *m_userAvatar = nullptr;            // 当前用户头像占位，后续可替换为真实头像图片。
    int m_currentSection = Messages;            // 默认选中“消息”，与 HTML 样稿一致。
};
