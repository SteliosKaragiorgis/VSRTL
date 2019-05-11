#include "vsrtl_mainwindow.h"
#include "core/vsrtl_design.h"
#include "ui_vsrtl_mainwindow.h"
#include "vsrtl_netlist.h"
#include "vsrtl_netlistmodel.h"
#include "vsrtl_widget.h"

#include <QAction>
#include <QHeaderView>
#include <QLineEdit>
#include <QSpinBox>
#include <QTimer>
#include <QToolBar>

#include <QSplitter>

#include <QTreeView>

namespace vsrtl {

MainWindow::MainWindow(Design& arch, QWidget* parent) : QMainWindow(parent), ui(new Ui::MainWindow) {
    ui->setupUi(this);
    setWindowState(Qt::WindowMaximized);
    m_vsrtlWidget = new VSRTLWidget(arch, this);

    m_netlist = new Netlist(arch, this);

    QSplitter* splitter = new QSplitter(this);

    splitter->addWidget(m_netlist);
    splitter->addWidget(m_vsrtlWidget);

    connect(m_netlist, &Netlist::selectionChanged, m_vsrtlWidget, &VSRTLWidget::handleSelectionChanged);
    connect(m_vsrtlWidget, &VSRTLWidget::componentSelectionChanged, m_netlist, &Netlist::updateSelection);

    setCentralWidget(splitter);

    createToolbar();

    setWindowTitle("VSRTL - Visual Simulation of Register Transfer Logic");
}

MainWindow::~MainWindow() {
    delete ui;
    delete m_vsrtlWidget;
}

void MainWindow::createToolbar() {
    QToolBar* simulatorToolBar = addToolBar("Simulator");

    const QIcon resetIcon = QIcon(":/icons/reset.svg");
    QAction* resetAct = new QAction(resetIcon, "Reset", this);
    connect(resetAct, &QAction::triggered, [this] {
        m_vsrtlWidget->reset();
        m_netlist->reloadNetlist();
    });
    resetAct->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_R));
    simulatorToolBar->addAction(resetAct);

    const QIcon rewindIcon = QIcon(":/icons/rewind.svg");
    QAction* rewindAct = new QAction(rewindIcon, "Rewind", this);
    connect(rewindAct, &QAction::triggered, [this] {
        m_vsrtlWidget->rewind();
        m_netlist->reloadNetlist();
    });
    rewindAct->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_Z));
    simulatorToolBar->addAction(rewindAct);
    rewindAct->setEnabled(false);
    connect(m_vsrtlWidget, &VSRTLWidget::canrewind, rewindAct, &QAction::setEnabled);

    const QIcon clockIcon = QIcon(":/icons/step.svg");
    QAction* clockAct = new QAction(clockIcon, "Clock", this);
    connect(clockAct, &QAction::triggered, [this] {
        m_vsrtlWidget->clock();
        m_netlist->reloadNetlist();
    });
    clockAct->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_C));
    simulatorToolBar->addAction(clockAct);

    QTimer* timer = new QTimer();
    connect(timer, &QTimer::timeout, clockAct, &QAction::trigger);

    const QIcon startTimerIcon = QIcon(":/icons/step-clock.svg");
    const QIcon stopTimerIcon = QIcon(":/icons/stop-clock.svg");
    QAction* clockTimerAct = new QAction(startTimerIcon, "Auto Clock", this);
    clockTimerAct->setCheckable(true);
    clockTimerAct->setChecked(false);
    connect(clockTimerAct, &QAction::triggered, [=] {
        if (timer->isActive()) {
            timer->stop();
            clockTimerAct->setIcon(startTimerIcon);
        } else {
            timer->start();
            clockTimerAct->setIcon(stopTimerIcon);
        }
    });

    simulatorToolBar->addAction(clockTimerAct);

    QSpinBox* stepSpinBox = new QSpinBox();
    stepSpinBox->setRange(1, 10000);
    stepSpinBox->setSuffix(" ms");
    stepSpinBox->setToolTip("Auto clock interval");
    connect(stepSpinBox, qOverload<int>(&QSpinBox::valueChanged), [timer](int msec) { timer->setInterval(msec); });
    stepSpinBox->setValue(100);

    simulatorToolBar->addWidget(stepSpinBox);

    simulatorToolBar->addSeparator();

    const QIcon showNetlistIcon = QIcon(":/icons/list.svg");
    QAction* showNetlist = new QAction(showNetlistIcon, "Show Netlist", this);
    connect(showNetlist, &QAction::triggered, [this] {
        if (m_netlist->isVisible()) {
            m_netlist->hide();
        } else {
            m_netlist->show();
        }
    });
    simulatorToolBar->addAction(showNetlist);
}  // namespace vsrtl

}  // namespace vsrtl
