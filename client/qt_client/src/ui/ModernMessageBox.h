#pragma once

#include <QDialog>
#include <QString>

class QCloseEvent;
class QKeyEvent;
class QShowEvent;
class QLabel;
class QPushButton;
class QWidget;

// 统一的紫灰主题提示框，替代原生 QMessageBox。
// 使用静态 showMessage() 可以像原先调用 QMessageBox::warning/information 一样简单。
class ModernMessageBox : public QDialog
{
    Q_OBJECT

public:
    // 创建并以模态方式显示提示框；用户点击“我知道了”后才会关闭。
    static void showMessage(QWidget *parent,
                            const QString &title,
                            const QString &message,
                            bool isError = false);

    explicit ModernMessageBox(QWidget *parent,
                              const QString &title,
                              const QString &message,
                              bool isError = false);

public slots:
    // 阻止 Esc 键触发 QDialog 默认的 reject 行为。
    void reject() override;

protected:
    // 窗口显示时，将它居中放置在父窗口上；没有可见父窗口时居中显示在屏幕上。
    void showEvent(QShowEvent *event) override;

    // 没有点击确认按钮时，不允许通过 Alt+F4 等方式关闭提示框。
    void closeEvent(QCloseEvent *event) override;

    // 显式拦截 Esc，避免不同平台的键盘行为绕过关闭规则。
    void keyPressEvent(QKeyEvent *event) override;

private:
    void buildInterface(const QString &title,
                        const QString &message,
                        bool isError);
    void centerOnParentOrScreen();
    void closeFromConfirmButton();

private:
    QWidget *m_card = nullptr;
    QLabel *m_titleLabel = nullptr;
    QLabel *m_messageLabel = nullptr;
    QPushButton *m_confirmButton = nullptr;

    // 只有点击“我知道了”按钮时才改为 true。
    bool m_confirmButtonClicked = false;
};
