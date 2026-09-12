/*
    SPDX-FileCopyrightText: 2026 iwinoid

    SPDX-License-Identifier: GPL-3.0-or-later

    配置面板。控件用代码搭而不用 .ui：
    项数不多，且 KConfigDialogManager 只要求控件的 objectName 是
    "kcfg_<条目名>"，代码搭反而更清楚、也少一个 uic 环节。

    「演示模式」那组控件**故意不带 kcfg_ 前缀**，因此不由
    KConfigDialogManager 自动绑定，而是在 load()/save() 里自己处理 ——
    它对应 ForceAngleDeg 一项，但界面上是"勾选框 + 角度"两个控件。
*/

#include "hinge_glassconfig.h"
#include "hingestate.h"

#include <KCModule>
#include <KLocalizedString>
#include <KPluginFactory>
#include <kwineffects_interface.h>

#include <QCheckBox>
#include <QDBusConnection>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QWidget>

namespace
{

QSpinBox *makeSpin(QWidget *parent, const QString &kcfgName, int min, int max, const QString &suffix)
{
    auto *box = new QSpinBox(parent);
    box->setObjectName(QStringLiteral("kcfg_") + kcfgName); // KConfigDialogManager 靠这个绑定
    box->setRange(min, max);
    box->setSuffix(suffix);
    box->setKeyboardTracking(false);
    return box;
}

QDoubleSpinBox *makeDoubleSpin(QWidget *parent, const QString &kcfgName, double min, double max,
                               int decimals, const QString &suffix, double step)
{
    auto *box = new QDoubleSpinBox(parent);
    box->setObjectName(QStringLiteral("kcfg_") + kcfgName);
    box->setRange(min, max);
    box->setDecimals(decimals);
    box->setSingleStep(step);
    box->setSuffix(suffix);
    box->setKeyboardTracking(false);
    return box;
}

QCheckBox *makeCheck(QWidget *parent, const QString &kcfgName, const QString &text)
{
    auto *box = new QCheckBox(text, parent);
    box->setObjectName(QStringLiteral("kcfg_") + kcfgName);
    return box;
}

/// 不参与 kcfg 自动绑定的控件（演示模式用）
QDoubleSpinBox *makePlainDoubleSpin(QWidget *parent, double min, double max, int decimals,
                                    const QString &suffix, double step)
{
    auto *box = new QDoubleSpinBox(parent);
    box->setRange(min, max);
    box->setDecimals(decimals);
    box->setSingleStep(step);
    box->setSuffix(suffix);
    box->setKeyboardTracking(false);
    return box;
}

} // namespace

class HingeGlassConfig : public KCModule
{
    Q_OBJECT

public:
    HingeGlassConfig(QObject *parent, const KPluginMetaData &data)
        : KCModule(parent, data)
    {
        auto *root = new QVBoxLayout(widget());

        // ---- 触发 ----
        auto *trigger = new QGroupBox(i18n("触发"), widget());
        auto *triggerForm = new QFormLayout(trigger);
        triggerForm->addRow(i18n("原角度："),
                            makeDoubleSpin(trigger, QStringLiteral("OriginalAngle"), 0.0, 360.0, 0,
                                           i18n("°"), 5.0));
        triggerForm->addRow(i18n("折叠角上限："),
                            makeDoubleSpin(trigger, QStringLiteral("MaxAngle"), 5.0, 90.0, 0,
                                           i18n("°"), 5.0));
        triggerForm->addRow(i18n("判定已停手的容差："),
                            makeDoubleSpin(trigger, QStringLiteral("StillToleranceDeg"), 0.0, 30.0, 1,
                                           i18n("°"), 0.5));
        auto *hint = new QLabel(
            i18n("角度高于「原角度」时完全无效果，低于它才出现，越往下越强。"
                 "把它设成你日常使用的角度，就能避免打字挪动笔记本时误触发。\n"
                 "效果淡出后，再往下折就会重新出现。"),
            trigger);
        hint->setWordWrap(true);
        triggerForm->addRow(QString(), hint);
        root->addWidget(trigger);

        // ---- 折叠几何 ----
        auto *geom = new QGroupBox(i18n("折叠几何"), widget());
        auto *geomForm = new QFormLayout(geom);
        geomForm->addRow(i18n("转轴位置："),
                         makeDoubleSpin(geom, QStringLiteral("HingePosition"), -0.5, 0.5, 3,
                                        QString(), 0.01));
        auto *geomHint = new QLabel(
            i18n("有效圆心距屏幕底边的距离 ÷ 屏高。\n"
                 "0 = 圆心正好在屏幕底边：理想单转轴，底边不动、只有顶边画圆。\n"
                 "负数 = 圆心在底边下方：二合一的双转轴机器就是这样，"
                 "底边和顶边各画一个同心圆，整块屏幕都参与折叠。"
                 "屏幕高约 200mm 时，下方 16mm ≈ -0.08。\n"
                 "正数 = 圆心在屏幕内部，底边以下的部分保持正常。"),
            geom);
        geomHint->setWordWrap(true);
        geomForm->addRow(QString(), geomHint);
        root->addWidget(geom);

        // ---- 时长 ----
        auto *timing = new QGroupBox(i18n("时长"), widget());
        auto *timingForm = new QFormLayout(timing);

        m_dwellSpin = makeSpin(timing, QStringLiteral("DwellMs"), 0, 5000, i18n(" 毫秒"));
        timingForm->addRow(i18n("停手后保持特效："), m_dwellSpin);

        m_persistCheck = makeCheck(timing, QStringLiteral("PersistWhileFolded"),
                                   i18n("停手后永久保持（回到原角度才消失）"));
        timingForm->addRow(m_persistCheck);
        auto *dwellHint = new QLabel(
            i18n("勾选上面的复选框后，折下去就一直显示、不用扶着屏幕，适合演示；"
                 "下面三项随之失效。"),
            timing);
        dwellHint->setWordWrap(true);
        timingForm->addRow(QString(), dwellHint);

        // ---- 分段停手时长 ----
        auto *segLabel = new QLabel(
            i18n("按角度分段（可选）：角度低于某一段的上限时，用该段的停手时长。"
                 "不填则全程用上面的统一值。分段按角度从小到大排列。"),
            timing);
        segLabel->setWordWrap(true);
        timingForm->addRow(segLabel);

        m_segTable = new QTableWidget(0, 2, timing);
        m_segTable->setHorizontalHeaderLabels({i18n("角度上限"), i18n("停手时长")});
        m_segTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        m_segTable->verticalHeader()->setVisible(false);
        m_segTable->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_segTable->setMinimumHeight(110);
        timingForm->addRow(m_segTable);

        auto *segButtons = new QWidget(timing);
        auto *segLayout = new QHBoxLayout(segButtons);
        segLayout->setContentsMargins(0, 0, 0, 0);
        auto *addBtn = new QPushButton(i18n("添加分段"), segButtons);
        auto *delBtn = new QPushButton(i18n("删除选中"), segButtons);
        segLayout->addWidget(addBtn);
        segLayout->addWidget(delBtn);
        segLayout->addStretch();
        timingForm->addRow(segButtons);
        connect(addBtn, &QPushButton::clicked, this, [this] { addSegmentRow(100.0, 300); });
        connect(delBtn, &QPushButton::clicked, this, [this] {
            const int row = m_segTable->currentRow();
            if (row >= 0) {
                m_segTable->removeRow(row);
            }
        });

        m_fadeSpin = makeSpin(timing, QStringLiteral("FadeMs"), 0, 2000, i18n(" 毫秒"));
        timingForm->addRow(i18n("恢复正常显示用时："), m_fadeSpin);
        m_minEffectSpin = makeSpin(timing, QStringLiteral("MinEffectMs"), 0, 2000, i18n(" 毫秒"));
        timingForm->addRow(i18n("最短显示时长："), m_minEffectSpin);
        root->addWidget(timing);

        // ---- 外观 ----
        auto *look = new QGroupBox(i18n("外观"), widget());
        auto *lookForm = new QFormLayout(look);
        lookForm->addRow(i18n("霜化强度："),
                         makeDoubleSpin(look, QStringLiteral("Frost"), 0.0, 0.18, 3, QString(), 0.01));
        lookForm->addRow(i18n("眼距（透视强度）："),
                         makeDoubleSpin(look, QStringLiteral("Eye"), 1.0, 6.0, 2, QString(), 0.1));
        lookForm->addRow(i18n("霜化采样数："),
                         makeSpin(look, QStringLiteral("Taps"), 1, 24, QString()));
        auto *tapsHint = new QLabel(
            i18n("24 是原版做法。调低更省但模糊量也会减少，不只是画质变差。"), look);
        tapsHint->setWordWrap(true);
        lookForm->addRow(QString(), tapsHint);
        root->addWidget(look);

        // ---- 采样 ----
        auto *poll = new QGroupBox(i18n("采样与平滑"), widget());
        auto *pollForm = new QFormLayout(poll);
        pollForm->addRow(i18n("空闲采样率："),
                         makeSpin(poll, QStringLiteral("IdlePollHz"), 2, 30, i18n(" Hz")));
        pollForm->addRow(i18n("活动采样率："),
                         makeSpin(poll, QStringLiteral("ActivePollHz"), 10, 120, i18n(" Hz")));
        pollForm->addRow(i18n("角度平滑弹簧："),
                         makeDoubleSpin(poll, QStringLiteral("SpringFreq"), 4.0, 40.0, 0,
                                        i18n(" rad/s"), 1.0));
        root->addWidget(poll);

        // ---- 演示 ----
        auto *demo = new QGroupBox(i18n("演示"), widget());
        auto *demoForm = new QFormLayout(demo);
        m_demoCheck = new QCheckBox(i18n("固定显示折叠效果（忽略转轴，方便演示与调参）"), demo);
        demoForm->addRow(m_demoCheck);
        m_demoAngle = makePlainDoubleSpin(demo, 0.0, 90.0, 0, i18n("°"), 5.0);
        demoForm->addRow(i18n("演示角度："), m_demoAngle);
        auto *demoHint = new QLabel(i18n("不折屏幕也能看清效果。演示完记得取消勾选。"), demo);
        demoHint->setWordWrap(true);
        demoForm->addRow(QString(), demoHint);
        root->addWidget(demo);

        connect(m_demoCheck, &QCheckBox::toggled, m_demoAngle, &QWidget::setEnabled);
        connect(m_persistCheck, &QCheckBox::toggled, this, &HingeGlassConfig::updateTimingEnabled);
        root->addStretch();

        // KWin 的 KWIN_CONFIG 就是字面量 "kwinrc"（config-kwin.h:18）。
        // KCM 目标没有 KWin 的头文件路径，所以直接写常量。
        KWin::HingeGlassConfig::instance(QStringLiteral("kwinrc"));
        addConfig(KWin::HingeGlassConfig::self(), widget());
    }

    void load() override
    {
        KCModule::load();

        // 演示模式（对应 ForceAngleDeg 一项，但界面上是两个控件）
        const double force = KWin::HingeGlassConfig::forceAngleDeg();
        const bool demoOn = force > -900.0;
        m_demoCheck->setChecked(demoOn);
        m_demoAngle->setValue(demoOn ? force : 30.0);
        m_demoAngle->setEnabled(demoOn);

        // 分段表
        m_segTable->setRowCount(0);
        for (const auto &seg : HingeGlass::parseDwellSegments(
                 KWin::HingeGlassConfig::dwellSegments().toStdString())) {
            addSegmentRow(seg.maxAngleDeg, seg.dwellMs);
        }

        updateTimingEnabled();
    }

    void save() override
    {
        // 必须在 KCModule::save() 之前写回，基类才会把它写进 kwinrc
        KWin::HingeGlassConfig::setForceAngleDeg(m_demoCheck->isChecked()
                                                     ? m_demoAngle->value()
                                                     : -999.0);

        // 分段表序列化成 "角度:毫秒,..."，按角度升序
        QStringList parts;
        for (int row = 0; row < m_segTable->rowCount(); ++row) {
            const auto *ang = qobject_cast<QDoubleSpinBox *>(m_segTable->cellWidget(row, 0));
            const auto *ms = qobject_cast<QSpinBox *>(m_segTable->cellWidget(row, 1));
            if (ang && ms) {
                parts << QStringLiteral("%1:%2").arg(ang->value(), 0, 'f', 1).arg(ms->value());
            }
        }
        KWin::HingeGlassConfig::setDwellSegments(parts.join(QLatin1Char(',')));
        KCModule::save();

        // [Effect-*] 的改动不会自动触发特效 reconfigure（KWin 的 configChanged
        // 只监听 [Plugins] 组），必须显式通过 DBus 通知。
        OrgKdeKwinEffectsInterface interface(QStringLiteral("org.kde.KWin"),
                                             QStringLiteral("/Effects"),
                                             QDBusConnection::sessionBus());
        interface.reconfigureEffect(QStringLiteral("hinge_glass"));
    }

    void defaults() override
    {
        KCModule::defaults();
        m_demoCheck->setChecked(false);
        m_demoAngle->setValue(30.0);
        m_demoAngle->setEnabled(false);
        m_segTable->setRowCount(0);
        addSegmentRow(80.0, 2000);
        addSegmentRow(100.0, 300);
        updateTimingEnabled();
    }

private:
    /// 往分段表加一行（角度上限 + 停手时长）
    void addSegmentRow(double maxAngle, int dwellMs)
    {
        const int row = m_segTable->rowCount();
        m_segTable->insertRow(row);

        auto *ang = new QDoubleSpinBox(m_segTable);
        ang->setRange(1.0, 360.0);
        ang->setDecimals(1);
        ang->setSuffix(i18n("°"));
        ang->setValue(maxAngle);
        m_segTable->setCellWidget(row, 0, ang);

        auto *ms = new QSpinBox(m_segTable);
        ms->setRange(0, 10000);
        ms->setSuffix(i18n(" 毫秒"));
        ms->setValue(dwellMs);
        m_segTable->setCellWidget(row, 1, ms);
    }

private Q_SLOTS:
    /// 永久保持时，下面三项都不再参与，置灰以免误导
    void updateTimingEnabled()
    {
        const bool timed = !m_persistCheck->isChecked();
        m_dwellSpin->setEnabled(timed);
        m_fadeSpin->setEnabled(timed);
        m_minEffectSpin->setEnabled(timed);
    }

private:
    QSpinBox *m_dwellSpin = nullptr;
    QCheckBox *m_persistCheck = nullptr;
    QTableWidget *m_segTable = nullptr;
    QSpinBox *m_fadeSpin = nullptr;
    QSpinBox *m_minEffectSpin = nullptr;
    QCheckBox *m_demoCheck = nullptr;
    QDoubleSpinBox *m_demoAngle = nullptr;
};

K_PLUGIN_CLASS(HingeGlassConfig)

#include "hinge_glass_config.moc"
