/**********************************************************************
 *  MainWindow.cpp
 **********************************************************************
 * Copyright (C) 2017 MX Authors
 *
 * Authors: Adrian
 *          Dolphin_Oracle
 *          MX Linux <http://mxlinux.org>
 *
 * This file is part of mx-package-manager.
 *
 * mx-package-manager is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * mx-package-manager is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with mx-package-manager.  If not, see <http://www.gnu.org/licenses/>.
 **********************************************************************/


#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "versionnumber.h"

#include <QFileDialog>
#include <QScrollBar>
#include <QTextStream>
#include <QtXml/QtXml>
#include <QProgressBar>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QImageReader>

#include <QDebug>

MainWindow::MainWindow(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    setup();
}

MainWindow::~MainWindow()
{
    delete ui;
}

// Setup versious items first time program runs
void MainWindow::setup()
{
    ui->tabWidget->blockSignals(true);
    cmd = new Cmd(this);
    if (cmd->getOutput("arch") == "x86_64") {
        arch = "amd64";
    } else {
        arch = "i386";
    }
    setProgressDialog();
    lock_file = new LockFile("/var/lib/dpkg/lock");
    connect(qApp, &QApplication::aboutToQuit, this, &MainWindow::cleanup);
    version = getVersion("mx-package-manager");
    this->setWindowTitle(tr("MX Package Manager"));
    ui->tabWidget->setCurrentIndex(0);
    QStringList column_names;
    column_names << "" << "" << tr("Package") << tr("Info") << tr("Description");
    ui->treePopularApps->setHeaderLabels(column_names);
    ui->treeOther->hideColumn(5); // Status of the package: installed, upgradable, etc
    ui->treeOther->hideColumn(6); // Displayed status true/false
    ui->icon->setIcon(QIcon::fromTheme("software-update-available", QIcon(":/icons/software-update-available.png")));
    loadPmFiles();
    refreshPopularApps();
    connect(ui->searchPopular, &QLineEdit::textChanged, this, &MainWindow::findPackage);
    connect(ui->searchBox, &QLineEdit::textChanged, this, &MainWindow::findPackageOther);
    ui->searchPopular->setFocus();
    tree_stable = new QTreeWidget();
    tree_mx_test = new QTreeWidget();
    tree_backports = new QTreeWidget();
    updated_once = false;
    warning_displayed = false;
    clearUi();
    ui->tabWidget->blockSignals(false);
}

// Uninstall listed packages
void MainWindow::uninstall(const QString &names)
{
    this->hide();
    lock_file->unlock();
    qDebug() << "uninstall list: " << names;
    QString title = tr("Uninstalling packages...");
    cmd->run("x-terminal-emulator -T '" + title + "' -e apt-get remove " + names);
    lock_file->lock();
    refreshPopularApps();
    clearCache();
    this->show();
    if (ui->tabOtherRepos->isVisible()) {
        buildPackageLists();
    }
}

// Run apt-get update
bool MainWindow::update()
{
    lock_file->unlock();
    setConnections();
    progress->show();
    progress->setLabelText(tr("Running apt-get update... "));
    if (cmd->run("apt-get update") == 0) {
        lock_file->lock();
        updated_once = true;
        return true;
    }
    lock_file->lock();
    return false;
}

// Update interface when done loading info
void MainWindow::updateInterface()
{
    QList<QTreeWidgetItem *> upgr_list = ui->treeOther->findItems("upgradable", Qt::MatchExactly, 5);
    QList<QTreeWidgetItem *> inst_list = ui->treeOther->findItems("installed", Qt::MatchExactly, 5);
    ui->labelNumApps->setText(QString::number(ui->treeOther->topLevelItemCount()));
    ui->labelNumUpgr->setText(QString::number(upgr_list.count()));
    ui->labelNumInst->setText(QString::number(inst_list.count() + upgr_list.count()));

    if (upgr_list.count() > 0 && ui->radioStable->isChecked()) {
        ui->buttonUpgradeAll->show();
    } else {
        ui->buttonUpgradeAll->hide();
    }

    QApplication::setOverrideCursor(QCursor(Qt::ArrowCursor));
    ui->comboFilter->setEnabled(true);
    ui->buttonForceUpdate->setEnabled(true);
    ui->groupBox->setEnabled(true);
    progress->hide();
    ui->searchBox->setFocus();
    ui->treeOther->blockSignals(false);
    findPackageOther();
}

// Write the name of the apps in a temp file
QString MainWindow::writeTmpFile(QString apps)
{
    QFile file(tmp_dir + "/listapps");
    if(!file.open(QFile::WriteOnly)) {
        qDebug() << "Count not open file: " << file.fileName();
    }
    QTextStream stream(&file);
    stream << apps;
    file.close();
    return file.fileName();
}


// Set proc and timer connections
void MainWindow::setConnections()
{
    connect(cmd, &Cmd::runTime, this, &MainWindow::tock, Qt::UniqueConnection);  // processes runtime emited by Cmd to be used by a progress bar
    connect(cmd, &Cmd::started, this, &MainWindow::cmdStart, Qt::UniqueConnection);
    connect(cmd, &Cmd::finished, this, &MainWindow::cmdDone, Qt::UniqueConnection);
}


// Processes tick emited by Cmd to be used by a progress bar
void MainWindow::tock(int counter, int duration)
{
    int max_value;
    max_value = (duration != 0) ? duration : 10;
    bar->setMaximum(max_value);
    bar->setValue(counter % (max_value + 1));
}


// Load info from the .pm files
void MainWindow::loadPmFiles()
{
    QDomDocument doc;

    QStringList filter("*.pm");
    QDir dir("/usr/share/mx-package-manager-pkglist");
    QStringList pmfilelist = dir.entryList(filter);

    foreach (const QString &file_name, pmfilelist) {
        QFile file(dir.absolutePath() + "/" + file_name);
        if (!file.open(QFile::ReadOnly | QFile::Text)) {
            qDebug() << "Could not open: " << file.fileName();
        } else {
            if (!doc.setContent(&file)) {
                qDebug() << "Could not load document: " << file_name << "-- not valid XML?";
            } else {
                processDoc(doc);
            }
        }
        file.close();
    }
}

// Process dom documents (from .pm files)
void MainWindow::processDoc(const QDomDocument &doc)
{
    /*  Order items in list:
        0 "category"
        1 "name"
        2 "description"
        3 "installable"
        4 "screenshot"
        5 "preinstall"
        6 "install_package_names"
        7 "postinstall"
        8 "uninstall_package_names"
    */

    QString category;
    QString name;
    QString description;
    QString installable;
    QString screenshot;
    QString preinstall;
    QString postinstall;
    QString install_names;
    QString uninstall_names;
    QStringList list;

    QDomElement root = doc.firstChildElement("app");
    QDomElement element = root.firstChildElement();

    for (; !element.isNull(); element = element.nextSiblingElement()) {
        if (element.tagName() == "category") {
            category = element.text().trimmed();
        } else if (element.tagName() == "name") {
            name = element.text().trimmed();
        } else if (element.tagName() == "description") {
            description = element.text().trimmed();
        } else if (element.tagName() == "installable") {
            installable = element.text().trimmed();
        } else if (element.tagName() == "screenshot") {
            screenshot = element.text().trimmed();
        } else if (element.tagName() == "preinstall") {
            preinstall = element.text().trimmed();
        } else if (element.tagName() == "install_package_names") {
            install_names = element.text().trimmed();
            install_names.replace("\n", " ");
        } else if (element.tagName() == "postinstall") {
            postinstall = element.text().trimmed();
        } else if (element.tagName() == "uninstall_package_names") {
            uninstall_names = element.text().trimmed();
        }
    }
    // skip non-installable packages
    if ((installable == "64" && arch != "amd64") || (installable == "32" && arch != "i386")) {
        return;
    }
    list << category << name << description << installable << screenshot << preinstall
         << postinstall << install_names << uninstall_names;
    popular_apps << list;
}

// Reload and refresh interface
void MainWindow::refreshPopularApps()
{
    ui->treePopularApps->clear();
    ui->treeOther->clear();
    ui->searchPopular->clear();
    ui->searchBox->clear();
    ui->buttonInstall->setEnabled(false);
    ui->buttonUninstall->setEnabled(false);
    installed_packages = listInstalled();
    displayPopularApps();
}

// Setup progress dialog
void MainWindow::setProgressDialog()
{
    timer = new QTimer(this);
    progress = new QProgressDialog(this);
    bar = new QProgressBar(progress);
    progCancel = new QPushButton(tr("Cancel"));
    connect(progCancel, &QPushButton::clicked, this, &MainWindow::cancelDownload);
    progress->setWindowModality(Qt::WindowModal);
    progress->setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint |Qt::WindowSystemMenuHint | Qt::WindowStaysOnTopHint);
    progress->setCancelButton(progCancel);
    progCancel->setDisabled(true);
    progress->setLabelText(tr("Please wait..."));
    progress->setAutoClose(false);
    progress->setBar(bar);
    bar->setTextVisible(false);
}

// Display Popular Apps in the treePopularApps
void MainWindow::displayPopularApps()
{
    QTreeWidgetItem *topLevelItem = NULL;
    QTreeWidgetItem *childItem;

    foreach (const QStringList &list, popular_apps) {
        QString category = list.at(0);
        QString name = list.at(1);
        QString description = list.at(2);
        QString installable = list.at(3);
        QString screenshot = list.at(4);
        QString preinstall = list.at(5);
        QString postinstall = list.at(6);
        QString install_names = list.at(7);
        QString uninstall_names = list.at(8);

        // add package category if treePopularApps doesn't already have it
        if (ui->treePopularApps->findItems(category, Qt::MatchFixedString, 2).isEmpty()) {
            topLevelItem = new QTreeWidgetItem();
            topLevelItem->setText(2, category);
            ui->treePopularApps->addTopLevelItem(topLevelItem);
            // topLevelItem look
            QFont font;
            font.setBold(true);
            //topLevelItem->setForeground(2, QBrush(Qt::darkGreen));
            topLevelItem->setFont(2, font);
            topLevelItem->setIcon(0, QIcon::fromTheme("folder-green"));
        } else {
            topLevelItem = ui->treePopularApps->findItems(category, Qt::MatchFixedString, 2).at(0); //find first match; add the child there
        }
        // add package name as childItem to treePopularApps
        childItem = new QTreeWidgetItem(topLevelItem);
        childItem->setText(2, name);
        childItem->setIcon(3, QIcon::fromTheme("info"));

        // add checkboxes
        childItem->setFlags(childItem->flags() | Qt::ItemIsUserCheckable);
        childItem->setCheckState(1, Qt::Unchecked);

        // add description from file
        childItem->setText(4, description);

        // add install_names (not displayed)
        childItem->setText(5, install_names);

        // add uninstall_names (not displayed)
        childItem->setText(6, uninstall_names);

        // add screenshot url (not displayed)
        childItem->setText(7, screenshot);

        // gray out installed items
        if (checkInstalled(uninstall_names)) {
            childItem->setForeground(2, QBrush(Qt::gray));
            childItem->setForeground(4, QBrush(Qt::gray));
        }
    }
    for (int i = 0; i < 5; ++i) {
        ui->treePopularApps->resizeColumnToContents(i);
    }
    ui->treePopularApps->sortItems(2, Qt::AscendingOrder);
    connect(ui->treePopularApps, &QTreeWidget::itemClicked, this, &MainWindow::displayInfo, Qt::UniqueConnection);
}


// Display available packages
void MainWindow::displayPackages(bool force_refresh)
{
    QMap<QString, QStringList> list;
    if(ui->radioMXtest->isChecked()) {
        if (tree_mx_test->topLevelItemCount() != 0 && !force_refresh) {
            copyTree(tree_mx_test, ui->treeOther);
            updateInterface();
            return;
        }
        list = mx_list;
    } else if (ui->radioBackports->isChecked()) {
        if (tree_backports->topLevelItemCount() != 0 && !force_refresh) {
            copyTree(tree_backports, ui->treeOther);
            updateInterface();
            return;
        }
        list = backports_list;
    } else if (ui->radioStable->isChecked()) {
        if (tree_stable->topLevelItemCount() != 0 && !force_refresh) {
            copyTree(tree_stable, ui->treeOther);
            updateInterface();
            return;
        }
        list = stable_list;
    }
    progress->show();

    QHash<QString, VersionNumber> hashInstalled; // hash that contains (app_name, VersionNumber) returned by apt-cache policy
    QHash<QString, VersionNumber> hashCandidate; //hash that contains (app_name, VersionNumber) returned by apt-cache policy for candidates
    QString app_name;
    QString app_info;
    QString apps;
    QString app_ver;
    QString app_desc;
    VersionNumber installed;
    VersionNumber candidate;

    QTreeWidgetItem *widget_item;

    // create a list of apps, create a hash with app_name, app_info
    QMap<QString, QStringList>::iterator i;
    for (i = list.begin(); i != list.end(); ++i) {
        app_name = i.key();
        apps += app_name + " "; // all the apps
        app_ver = i.value().at(0);
        app_desc = i.value().at(1);

        widget_item = new QTreeWidgetItem(ui->treeOther);
        widget_item->setCheckState(0, Qt::Unchecked);
        widget_item->setText(2, app_name);
        widget_item->setText(3, app_ver);
        widget_item->setText(4, app_desc);
        widget_item->setText(6, "true"); // all items are displayed till filtered
    }
    for (int i = 0; i < ui->treeOther->columnCount(); ++i) {
        ui->treeOther->resizeColumnToContents(i);
    }
    QString tmp_file_name = writeTmpFile(apps);

    if (app_info_list.size() == 0 || force_refresh) {
        progress->setLabelText(tr("Updating package list..."));
        setConnections();
        QString info_installed = cmd->getOutput("LC_ALL=en_US.UTF-8 xargs apt-cache policy <" + tmp_file_name + "|grep Candidate -B2");
        app_info_list = info_installed.split("--"); // list of installed apps
    }
    // create a hash of name and installed version
    foreach(const QString &item, app_info_list) {
        app_name = item.section(":", 0, 0).trimmed();
        installed = item.section("\n  ", 1, 1).trimmed().section(": ", 1, 1); // Installed version
        candidate = item.section("\n  ", 2, 2).trimmed().section(": ", 1, 1);
        hashInstalled.insert(app_name, installed);
        hashCandidate.insert(app_name, candidate);
    }
    for (int i = 0; i < ui->treeOther->columnCount(); ++i) {
        ui->treeOther->resizeColumnToContents(i);
    }

    // process the entire list of apps
    QTreeWidgetItemIterator it(ui->treeOther);
    int upgr_count = 0;
    int inst_count = 0;

    // update tree
    while (*it) {
        app_name = (*it)->text(2);
        if (((app_name.startsWith("lib") && !app_name.startsWith("libreoffice")) || app_name.endsWith("-dev")) && ui->checkHideLibs->isChecked()) {
            (*it)->setHidden(true);
        }
        app_ver = (*it)->text(3);
        installed = hashInstalled[app_name];
        candidate = hashCandidate[app_name];
        VersionNumber repocandidate(app_ver); // candidate from the selected repo, might be different than the one from Stable

        (*it)->setIcon(1, QIcon()); // reset update icon
        if (installed.toString() == "(none)") {
            for (int i = 0; i < ui->treeOther->columnCount(); ++i) {
                (*it)->setToolTip(i, tr("Version ") + candidate.toString() + tr(" in stable repo"));
            }
            (*it)->setText(5, "not installed");
        } else if (installed.toString() == "") {
            for (int i = 0; i < ui->treeOther->columnCount(); ++i) {
               (*it)->setToolTip(i, tr("Not available in stable repo"));
            }
            (*it)->setText(5, "not installed");
        } else {
            inst_count++;
            if (installed >= repocandidate) {
                for (int i = 0; i < ui->treeOther->columnCount(); ++i) {
                    (*it)->setForeground(2, QBrush(Qt::gray));
                    (*it)->setForeground(4, QBrush(Qt::gray));
                    (*it)->setToolTip(i, tr("Latest version ") + installed.toString() + tr(" already installed"));
                }
                (*it)->setText(5, "installed");
            } else {
                (*it)->setIcon(1, QIcon::fromTheme("software-update-available", QIcon(":/icons/software-update-available.png")));
                for (int i = 0; i < ui->treeOther->columnCount(); ++i) {
                    (*it)->setToolTip(i, tr("Version ") + installed.toString() + tr(" installed"));
                }
                upgr_count++;
                (*it)->setText(5, "upgradable");
            }

        }
        ++it;
    }

    // cache trees for reuse
    if(ui->radioMXtest->isChecked()) {
        copyTree(ui->treeOther, tree_mx_test);
    } else if (ui->radioBackports->isChecked()) {
        copyTree(ui->treeOther, tree_backports);
    } else if (ui->radioStable->isChecked()) {
        copyTree(ui->treeOther, tree_stable);
    }
    updateInterface();
}

// Display warning for Debian Backports
void MainWindow::displayWarning()
{
    if (warning_displayed) {
        return;
    }
    QFileInfo checkfile(QDir::homePath() + "/.config/mx-debian-backports-installer");
    if (checkfile.exists()) {
        return;
    }
    QMessageBox msgBox(QMessageBox::Warning,
                       tr("Warning"),
                       tr("You are about to use Debian Backports, which contains packages taken from the next "\
                          "Debian release (called 'testing'), adjusted and recompiled for usage on Debian stable. "\
                          "They cannot be tested as extensively as in the stable releases of Debian and MX Linux, "\
                          "and are provided on an as-is basis, with risk of incompatibilities with other components "\
                          "in Debian stable. Use with care!"), 0, 0);
    msgBox.addButton(QMessageBox::Close);
    QCheckBox *cb = new QCheckBox();
    msgBox.setCheckBox(cb);
    cb->setText(tr("Do not show this message again"));
    connect(cb, &QCheckBox::clicked, this, &MainWindow::disableWarning);
    msgBox.exec();
    warning_displayed = true;
}

// What to do if the download failed was interrupted. Check if cache available and use it, otherwise display first tab
void MainWindow::ifDownloadFailed()
{
    progress->hide();
    if(ui->radioMXtest->isChecked()) {
        if (tree_mx_test->topLevelItemCount() != 0) {
            copyTree(tree_mx_test, ui->treeOther);
            updateInterface();
            return;
        } else {
            ui->tabWidget->setCurrentWidget(ui->tabApps);
        }
    } else if (ui->radioBackports->isChecked()) {
        if (tree_backports->topLevelItemCount() != 0) {
            copyTree(tree_backports, ui->treeOther);
            updateInterface();
            return;
        } else {
            ui->tabWidget->setCurrentWidget(ui->tabApps);
        }
    } else if (ui->radioStable->isChecked()) {
        if (tree_stable->topLevelItemCount() != 0) {
            copyTree(tree_stable, ui->treeOther);
            updateInterface();
            return;
        } else {
            ui->tabWidget->setCurrentWidget(ui->tabApps);
        }
    }
}

// Install the list of apps
void MainWindow::install(const QString &names)
{
    if (!checkOnline()) {
        QMessageBox::critical(this, tr("Error"), tr("Internet is not available, won't be able to download the list of packages"));
        return;
    }
    this->hide();
    lock_file->unlock();
    QString title = tr("Installing packages...");
    if (ui->radioBackports->isChecked()) {
        cmd->run("x-terminal-emulator -T '" +  title + "' -e apt-get install -t jessie-backports --reinstall " + names);
    } else {
        cmd->run("x-terminal-emulator -T '" +  title + "' -e apt-get install --reinstall " + names);
    }

    lock_file->lock();
    this->show();
}

// install a list of application and run postprocess for each of them.
void MainWindow::installBatch(const QStringList &name_list)
{
    QString postinstall;
    QString install_names;

    // load all the
    foreach (const QString name, name_list) {
        foreach (const QStringList &list, popular_apps) {
            if (list.at(1) == name) {
                postinstall += list.at(6) + "\n";
                install_names += list.at(7) + " ";
            }
        }
    }
    setConnections();

    if (install_names != "") {
        progress->hide();
        this->hide();
        progress->setLabelText(tr("Installing..."));
        install(install_names);
        this->show();
        progress->show();
    }
    setConnections();
    progress->setLabelText(tr("Post-processing..."));
    lock_file->unlock();
    cmd->run(postinstall);
    lock_file->lock();
    progress->hide();
}

// install named app
void MainWindow::installPopularApp(const QString &name)
{
    QString preinstall;
    QString postinstall;
    QString install_names;

    // get all the app info
    foreach (const QStringList &list, popular_apps) {
        if (list.at(1) == name) {
            preinstall = list.at(5);
            postinstall = list.at(6);
            install_names = list.at(7);
        }
    }
    setConnections();
    progress->setLabelText(tr("Pre-processing for ") + name);
    lock_file->unlock();
    cmd->run(preinstall);

    if (install_names != "") {
        progress->hide();
        this->hide();
        progress->setLabelText(tr("Installing ") + name);
        install(install_names);
        this->show();
        progress->show();
    }
    setConnections();
    progress->setLabelText(tr("Post-processing for ") + name);
    lock_file->unlock();
    cmd->run(postinstall);
    lock_file->lock();
    progress->hide();
}


// Process checked items to install
void MainWindow::installPopularApps()
{
    QStringList batch_names;

    if (!checkOnline()) {
        QMessageBox::critical(this, tr("Error"), tr("Internet is not available, won't be able to download the list of packages"));
        return;
    }
    if (!updated_once) {
        update();
    }

    // make a list of apps to be installed together
    QTreeWidgetItemIterator it(ui->treePopularApps);
    while (*it) {
        if ((*it)->checkState(1) == Qt::Checked) {
            QString name = (*it)->text(2);
            foreach (const QStringList &list, popular_apps) {
                if (list.at(1) == name) {
                    QString preinstall = list.at(5);
                    if (preinstall == "") {  // add to batch processing if there is not preinstall command
                        batch_names << name;
                        (*it)->setCheckState(1, Qt::Unchecked);
                    }
                }
            }
        }
        ++it;
    }
    installBatch(batch_names);

    // install the rest of the apps
    QTreeWidgetItemIterator iter(ui->treePopularApps);
    while (*iter) {
        if ((*iter)->checkState(1) == Qt::Checked) {
            installPopularApp((*iter)->text(2));
        }
        ++iter;
    }
    setCursor(QCursor(Qt::ArrowCursor));
    if (QMessageBox::information(this, tr("Done"),
                                 tr("Process finished.<p><b>Do you want to exit MX Package Installer?</b>"),
                                 tr("Yes"), tr("No")) == 0){
        qApp->exit(0);
    }
    refreshPopularApps();
    clearCache();
    this->show();
}

// Install selected items from ui->treeOther
void MainWindow::installSelected()
{
    QString names = change_list.join(" ");

    // change sources as needed
    if(ui->radioMXtest->isChecked()) {
        cmd->run("echo deb http://main.mepis-deb.org/mx/testrepo/ mx15 test>>/etc/apt/sources.list.d/mxpm-temp.list");
        //enable mx16 repo if necessary
        if (system("cat /etc/apt/sources.list.d/*.list |grep -q mx16") == 0) {
            cmd->run("echo deb http://main.mepis-deb.org/mx/testrepo/ mx16 test>>/etc/apt/sources.list.d/mxpm-temp.list");
        }
        update();
    } else if (ui->radioBackports->isChecked()) {
        cmd->run("echo deb http://ftp.debian.org/debian jessie-backports main contrib non-free>/etc/apt/sources.list.d/mxpm-temp.list");
        update();
    }
    progress->hide();
    install(names);
    if (ui->radioMXtest->isChecked() || ui->radioBackports->isChecked()) {
        cmd->run("rm -f /etc/apt/sources.list.d/mxpm-temp.list");
        update();
    }
    change_list.clear();
    clearCache();
    buildPackageLists();
}

// Check if online
bool MainWindow::checkOnline()
{
    return(system("wget -q --spider http://google.com >/dev/null 2>&1") == 0);
}

// Build the list of available packages from various source
bool MainWindow::buildPackageLists(bool force_download)
{
    clearUi();
    ui->treeOther->blockSignals(true);
    if (!downloadPackageList(force_download)) {
        ifDownloadFailed();
        return false;
    }
    if (!readPackageList(force_download)) {
        ifDownloadFailed();
        return false;
    }
    displayPackages(force_download);
    return true;
}

// Download the Packages.gz from sources
bool MainWindow::downloadPackageList(bool force_download)
{
    if (!checkOnline()) {
        QMessageBox::critical(this, tr("Error"), tr("Internet is not available, won't be able to download the list of packages"));
        return false;
    }
    if (tmp_dir == "") {
        tmp_dir = cmd->getOutput("mktemp -d /tmp/mxpm-XXXXXXXX");
    }
    QDir::setCurrent(tmp_dir);
    setConnections();
    progress->setLabelText(tr("Downloading package info..."));
    progCancel->setEnabled(true);
    if (ui->radioStable->isChecked()) {
        if (stable_raw.isEmpty() || force_download) {
            if (force_download) {
                if (!update()) {
                    return false;
                }
            }
            progress->show();
            if (cmd->run("LC_ALL=en_US.UTF-8 apt-cache dumpavail") == 0) {
                stable_raw = cmd->getOutput();
            } else {
                return false;
            }
        }
    } else if (ui->radioMXtest->isChecked())  {
        if (!QFile(tmp_dir + "/mx15Packages").exists() || force_download) {
            progress->show();
            if (cmd->run("wget http://mxrepo.com/mx/testrepo/dists/mx15/test/binary-" + arch +
                             "/Packages.gz -O mx15Packages.gz && gzip -df mx15Packages.gz") != 0) {
                QFile::remove(tmp_dir + "/mx15Packages.gz");
                QFile::remove(tmp_dir + "/mx15Packages");
                return false;
            }
        }
    } else {
        if (!QFile(tmp_dir + "/mainPackages").exists() ||
                !QFile(tmp_dir + "/contribPackages").exists() ||
                !QFile(tmp_dir + "/nonfreePackages").exists() || force_download) {
            progress->show();
            int err = cmd->run("wget ftp://ftp.us.debian.org/debian/dists/jessie-backports/main/binary-" + arch +
                                "/Packages.gz -O mainPackages.gz && gzip -df mainPackages.gz");
            if (err != 0 ) {
                QFile::remove(tmp_dir + "/mainPackages.gz");
                QFile::remove(tmp_dir + "/mainPackages");
                return false;
            }
            err = cmd->run("wget ftp://ftp.us.debian.org/debian/dists/jessie-backports/contrib/binary-" + arch +
                                "/Packages.gz -O contribPackages.gz && gzip -df contribPackages.gz");
            if (err != 0 ) {
                QFile::remove(tmp_dir + "/contribPackages.gz");
                QFile::remove(tmp_dir + "/contribPackages");
                return false;
            }
            err = cmd->run("wget ftp://ftp.us.debian.org/debian/dists/jessie-backports/non-free/binary-" + arch +
                                "/Packages.gz -O nonfreePackages.gz && gzip -df nonfreePackages.gz");
            if (err != 0 ) {
                QFile::remove(tmp_dir + "/nonfreePackages.gz");
                QFile::remove(tmp_dir + "/nonfreePackages");
                return false;
            }
            progCancel->setDisabled(true);
            cmd->run("cat mainPackages + contribPackages + nonfreePackages > allPackages");
        }
    }
    return true;
}

// Process downloaded *Packages.gz files
bool MainWindow::readPackageList(bool force_download)
{
    QFile file;
    QMap<QString, QStringList> map;
    QStringList list;
    QStringList package_list;
    QStringList version_list;
    QStringList description_list;

    progCancel->setDisabled(true);
    // don't process if the lists are populate
    if (!(stable_list.isEmpty() || mx_list.isEmpty() || backports_list.isEmpty() || force_download)) {
        return true;
    }
    if (ui->radioStable->isChecked()) { // read Stable list
        list = stable_raw.split("\n");
    } else {
         progress->show();
         progress->setLabelText(tr("Reading downloaded file..."));
         if (ui->radioMXtest->isChecked())  { // read MX Test list
             file.setFileName(tmp_dir + "/mx15Packages");
         } else if (ui->radioBackports->isChecked()) {  // read Backports lsit
             file.setFileName(tmp_dir + "/allPackages");
         }
         if(!file.open(QFile::ReadOnly)) {
             qDebug() << "Count not open file: " << file.fileName();
             return false;
         }
         QString file_content = file.readAll();
         list = file_content.split("\n");
         file.close();
    }
    foreach(QString line, list) {
        if (line.startsWith("Package: ")) {
            package_list << line.remove("Package: ");
        } else if (line.startsWith("Version: ")) {
            version_list << line.remove("Version: ");
        } else if (line.startsWith("Description: ")) {
            description_list << line.remove("Description: ");
        }
    }
    for (int i = 0; i < package_list.size(); ++i) {
        map.insert(package_list.at(i), QStringList() << version_list.at(i) << description_list.at(i));
    }
    if (ui->radioStable->isChecked()) {
        stable_list = map;
    } else if (ui->radioMXtest->isChecked())  {
        mx_list = map;
    } else if (ui->radioBackports->isChecked()) {
        backports_list = map;
    }
    return true;
}

// Cancel download
void MainWindow::cancelDownload()
{
    qDebug() << "cancel download";
    cmd->terminate();
}

// Clear UI when building package list
void MainWindow::clearUi()
{
    ui->comboFilter->setEnabled(false);
    ui->comboFilter->setCurrentIndex(0);
    ui->groupBox->setEnabled(false);

    ui->labelNumApps->setText("");
    ui->labelNumInst->setText("");
    ui->labelNumUpgr->setText("");

    ui->buttonCancel->setEnabled(true);
    ui->buttonInstall->setEnabled(false);
    ui->buttonUninstall->setEnabled(false);
    ui->buttonUpgradeAll->setHidden(true);
    ui->buttonForceUpdate->setEnabled(false);

    ui->searchBox->clear();
    ui->treeOther->clear();
}

// Copy QTreeWidgets
void MainWindow::copyTree(QTreeWidget *from, QTreeWidget *to)
{

    to->clear();
    QTreeWidgetItem *item;
    QTreeWidgetItemIterator it(from);
    while (*it) {
        item = new QTreeWidgetItem();
        item = (*it)->clone();
        to->addTopLevelItem(item);
        ++it;
    }
}

// Cleanup environment when window is closed
void MainWindow::cleanup()
{
    qDebug() << "cleanup code";
    if(!cmd->terminate()) {
        cmd->kill();
    }
    qDebug() << "removing lock";
    lock_file->unlock();
    QDir::setCurrent("/");
    if (tmp_dir.startsWith("/tmp/mxpm-")) {
        qDebug() << "removing tmp folder";
        system("rm -r " + tmp_dir.toUtf8());
    }
}

// Clear cached trees
void MainWindow::clearCache()
{
    tree_stable->clear();
    tree_mx_test->clear();
    tree_backports->clear();
    app_info_list.clear();
    if (!QFile::remove(tmp_dir + "/listapps")) {
        qDebug() << "could not remove listapps file";
    }
    qDebug() << "tree cleared";
}

// Get version of the program
QString MainWindow::getVersion(QString name)
{
    return cmd->getOutput("dpkg -l "+ name + "| awk 'NR==6 {print $3}'");
}

// Return true if all the packages listed are installed
bool MainWindow::checkInstalled(const QString &names)
{
    if (names == "") {
        return false;
    }
    foreach(const QString &name, names.split("\n")) {
        if (!installed_packages.contains(name)) {
            return false;
        }
    }
    return true;
}

// Return true if all the packages in the list are installed
bool MainWindow::checkInstalled(const QStringList &name_list)
{
    if (name_list.size() == 0) {
        return false;
    }
    foreach(const QString &name, name_list) {
        if (!installed_packages.contains(name)) {
            return false;
        }
    }
    return true;
}

// return true if all the items in the list are upgradable
bool MainWindow::checkUpgradable(const QStringList &name_list)
{
    if (name_list.size() == 0) {
        return false;
    }
    QList<QTreeWidgetItem *> item_list;
    foreach(const QString &name, name_list) {
        item_list = ui->treeOther->findItems(name, Qt::MatchExactly, 2);
        if (item_list.isEmpty()) {
            return false;
        }
        if (item_list.at(0)->text(5) != "upgradable") {
            return false;
        }
    }
    return true;
}


// Returns list of all installed packages
QStringList MainWindow::listInstalled()
{
    QString str = cmd->getOutput("dpkg --get-selections | grep -v deinstall | cut -f1");
    str.remove(":i386");
    str.remove(":amd64");
    return str.split("\n");
}

void MainWindow::cmdStart()
{
    setCursor(QCursor(Qt::BusyCursor));
}


void MainWindow::cmdDone()
{
    setCursor(QCursor(Qt::ArrowCursor));
    cmd->disconnect();
}

// Disable Backports warning
void MainWindow::disableWarning(bool checked)
{
    if (checked) {
        system("touch " + QDir::homePath().toUtf8() + "/.config/mx-debian-backports-installer");
    }
}

// Display info when clicking the "info" icon of the package
void MainWindow::displayInfo(QTreeWidgetItem *item, int column)
{
    if (column == 3 && item->childCount() == 0) {
        QString desc = item->text(4);
        QString install_names = item->text(5);
        QString title = item->text(2);
        QString msg = "<b>" + title + "</b><p>" + desc + "<p>" ;
        if (install_names != 0) {
            msg += tr("Packages to be installed: ") + install_names;
        }
        QUrl url = item->text(7); // screenshot url

        if (!url.isValid() || url.isEmpty() || url.url() == "none") {
            qDebug() << "no screenshot for: " << title;
        } else {
            QNetworkAccessManager *manager = new QNetworkAccessManager(this);
            QNetworkReply* reply = manager->get(QNetworkRequest(url));

            QEventLoop loop;
            connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
            timer->start(5000);
            connect(timer, &QTimer::timeout, &loop, &QEventLoop::quit);
            ui->treePopularApps->blockSignals(true);
            loop.exec();
            timer->stop();
            ui->treePopularApps->blockSignals(false);

            if (reply->error())
            {
                qDebug() << "Download of " << url.url() << " failed: " << qPrintable(reply->errorString());
            } else {
                QImage image;
                QByteArray data;
                QBuffer buffer(&data);
                QImageReader imageReader(reply);
                image = imageReader.read();
                if (imageReader.error()) {
                    qDebug() << "loading screenshot: " << imageReader.errorString();
                } else {
                    image = image.scaled(QSize(200,300), Qt::KeepAspectRatioByExpanding);
                    image.save(&buffer, "PNG");
                    msg += QString("<p><img src='data:image/png;base64, %0'>").arg(QString(data.toBase64()));
                }
            }
        }
        QMessageBox info(QMessageBox::NoIcon, tr("Package info") , msg, QMessageBox::Close, this);
        info.exec();
    }
}

// Find package in view
void MainWindow::findPackage()
{
    QTreeWidgetItemIterator it(ui->treePopularApps);
    QString word = ui->searchPopular->text();
    if (word == "") {
        while (*it) {
            (*it)->setExpanded(false);
            ++it;
        }
        ui->treePopularApps->reset();
        for (int i = 0; i < 5; ++i) {
            ui->treePopularApps->resizeColumnToContents(i);
        }
        return;
    }
    QList<QTreeWidgetItem *> found_items = ui->treePopularApps->findItems(word, Qt::MatchContains|Qt::MatchRecursive, 2);

    // hide/unhide items
    while (*it) {
        if ((*it)->childCount() == 0) { // if child
            if (found_items.contains(*it)) {
                (*it)->setHidden(false);
          } else {
                (*it)->parent()->setHidden(true);
                (*it)->setHidden(true);
            }
        }
        ++it;
    }

    // process found items
    foreach(QTreeWidgetItem *item, found_items) {
        if (item->childCount() == 0) { // if child, expand parent
            item->parent()->setExpanded(true);
            item->parent()->setHidden(false);
        } else {  // if parent, expand children
            item->setExpanded(true);
            item->setHidden(false);
            int count = item->childCount();
            for (int i = 0; i < count; ++i ) {
                item->child(i)->setHidden(false);
            }
        }
    }
    for (int i = 0; i < 5; ++i) {
        ui->treePopularApps->resizeColumnToContents(i);
    }
}

// Find packages in the second tab (other sources)
void MainWindow::findPackageOther()
{
    QString word = ui->searchBox->text();
    QList<QTreeWidgetItem *> found_items = ui->treeOther->findItems(word, Qt::MatchContains, 2);
    QTreeWidgetItemIterator it(ui->treeOther);
    while (*it) {
      if ((*it)->text(6) == "true" && found_items.contains(*it)) {
          (*it)->setHidden(false);
      } else {
          (*it)->setHidden(true);
      }
      // hide libs
      QString app_name = (*it)->text(2);
      if (((app_name.startsWith("lib") && !app_name.startsWith("libreoffice")) || app_name.endsWith("-dev")) && ui->checkHideLibs->isChecked()) {
          (*it)->setHidden(true);
      }
      ++it;
    }
}

// Install button clicked
void MainWindow::on_buttonInstall_clicked()
{
    if (ui->tabApps->isVisible()) {
        installPopularApps();
    } else {
        installSelected();
    }
}

// About button clicked
void MainWindow::on_buttonAbout_clicked()
{
    this->hide();
    QMessageBox msgBox(QMessageBox::NoIcon,
                       tr("About MX Package Manager"), "<p align=\"center\"><b><h2>" +
                       tr("MX Package Manager") + "</h2></b></p><p align=\"center\">" + tr("Version: ") + version + "</p><p align=\"center\"><h3>" +
                       tr("Package Manager for MX Linux") +
                       "</h3></p><p align=\"center\"><a href=\"http://mxlinux.org\">http://mxlinux.org</a><br /></p><p align=\"center\">" +
                       tr("Copyright (c) MX Linux") + "<br /><br /></p>");
    msgBox.addButton(tr("License"), QMessageBox::AcceptRole);
    msgBox.addButton(tr("Cancel"), QMessageBox::NoRole);
    if (msgBox.exec() == QMessageBox::AcceptRole) {
        system("mx-viewer file:///usr/share/doc/mx-package-manager/license.html '" + tr("MX Package Manager").toUtf8() + " " + tr("License").toUtf8() + "'");
    }
    this->show();
}
// Help button clicked
void MainWindow::on_buttonHelp_clicked()
{
    this->hide();
    QString cmd = QString("mx-viewer https://mxlinux.org/wiki/help-files/help-mx-package-installer '%1'").arg(tr("MX Package Manager"));
    system(cmd.toUtf8());
    this->show();
}

// Resize columns when expanding
void MainWindow::on_treePopularApps_expanded()
{
    ui->treePopularApps->resizeColumnToContents(2);
    ui->treePopularApps->resizeColumnToContents(4);
}

// Tree item clicked
void MainWindow::on_treePopularApps_itemClicked()
{
    bool checked = false;
    bool installed = true;

    QTreeWidgetItemIterator it(ui->treePopularApps);
    while (*it) {
        if ((*it)->checkState(1) == Qt::Checked) {
            checked = true;
            if ((*it)->foreground(2) != Qt::gray) {
                installed = false;
            }
        }
        ++it;
    }
    ui->buttonInstall->setEnabled(checked);
    ui->buttonUninstall->setEnabled(checked && installed);
    if (checked && installed) {
        ui->buttonInstall->setText(tr("Reinstall"));
    } else {
        ui->buttonInstall->setText(tr("Install"));
    }
}

// Tree item expanded
void MainWindow::on_treePopularApps_itemExpanded(QTreeWidgetItem *item)
{
    item->setIcon(0, QIcon::fromTheme("folder-open"));
    ui->treePopularApps->resizeColumnToContents(2);
    ui->treePopularApps->resizeColumnToContents(4);
}

// Tree item collapsed
void MainWindow::on_treePopularApps_itemCollapsed(QTreeWidgetItem *item)
{
    item->setIcon(0, QIcon::fromTheme("folder-green"));
    ui->treePopularApps->resizeColumnToContents(2);
    ui->treePopularApps->resizeColumnToContents(4);
}


// Uninstall clicked
void MainWindow::on_buttonUninstall_clicked()
{
    QString names;
    if (ui->tabApps->isVisible()) {
        QTreeWidgetItemIterator it(ui->treePopularApps);
        while (*it) {
            if ((*it)->checkState(1) == Qt::Checked) {
                names += (*it)->text(6).replace("\n", " ") + " ";
            }
            ++it;
        }
    } else if (ui->tabOtherRepos->isVisible()) {
        names = change_list.join(" ");
    }
    uninstall(names);
}

// Actions on switching the tabs
void MainWindow::on_tabWidget_currentChanged(int index)
{
    if (index == 1) {
        // show select message if the current tree is not cached
        if ((ui->radioStable->isChecked() && tree_stable->topLevelItemCount() == 0 ) ||
            (ui->radioMXtest->isChecked() && tree_mx_test->topLevelItemCount() == 0 ) ||
                    (ui->radioBackports->isChecked() && tree_backports->topLevelItemCount() == 0))
        {
            QMessageBox msgBox(QMessageBox::Question,
                               tr("Repo Selection"),
                               tr("Plese select repo to load"));
            msgBox.addButton(tr("Debian Backports Repo"), QMessageBox::AcceptRole);
            msgBox.addButton(tr("MX Test Repo"), QMessageBox::AcceptRole);
            msgBox.addButton(tr("Stable Repo"), QMessageBox::AcceptRole);
            msgBox.addButton(tr("Cancel"), QMessageBox::NoRole);
            int ret = msgBox.exec();
            switch (ret) {
            case 0:
                ui->radioBackports->blockSignals(true);
                ui->radioBackports->setChecked(true);
                ui->radioBackports->blockSignals(false);
                break;
            case 1:
                ui->radioMXtest->blockSignals(true);
                ui->radioMXtest->setChecked(true);
                ui->radioMXtest->blockSignals(false);
                break;
            case 2:
                ui->radioStable->blockSignals(true);
                ui->radioStable->setChecked(true);
                ui->radioStable->blockSignals(false);
                break;
            default:
                ui->tabWidget->setCurrentIndex(0);
                return;
            }
        }
        buildPackageLists();
    } if (index == 0) {
        ui->treePopularApps->clear();
        installed_packages = listInstalled();
        displayPopularApps();
    }
}


// Filter items according to selected filter
void MainWindow::on_comboFilter_activated(const QString &arg1)
{
    QList<QTreeWidgetItem *> found_items;
    QTreeWidgetItemIterator it(ui->treeOther);
    ui->treeOther->blockSignals(true);

    if (arg1 == tr("All packages")) {
        while (*it) {
            (*it)->setText(6, "true"); // Displayed flag
            (*it)->setHidden(false);
            ++it;
        }
        findPackageOther();
        ui->treeOther->blockSignals(false);
        return;
    }

    if (arg1 == tr("Upgradable")) {
        found_items = ui->treeOther->findItems("upgradable", Qt::MatchExactly, 5);
    } else if (arg1 == tr("Installed")) {
        found_items = ui->treeOther->findItems("installed", Qt::MatchExactly, 5);
    } else if (arg1 == tr("Not installed")) {
        found_items = ui->treeOther->findItems("not installed", Qt::MatchExactly, 5);
    }

    while (*it) {
        if (found_items.contains(*it) ) {
            (*it)->setHidden(false);
            (*it)->setText(6, "true"); // Displayed flag
        } else {
            (*it)->setHidden(true);
            (*it)->setText(6, "false");
        }
        ++it;
    }
    findPackageOther();
    ui->treeOther->blockSignals(false);
}

// When selecting on item in the list
void MainWindow::on_treeOther_itemChanged(QTreeWidgetItem *item)
{
    /* if all apps are uninstalled (or some installed) -> enable Install, disable Uinstall
     * if all apps are installed or upgradable -> enable Uninstall, enable Install
     * if all apps are upgradable -> change Install label to Upgrade;
     */

    QString newapp = QString(item->text(2));
    if (item->checkState(0) == Qt::Checked) {
        ui->buttonInstall->setEnabled(true);
        change_list.append(newapp);
    } else {
        change_list.removeOne(newapp);
    }

    if (!checkInstalled(change_list)) {
        ui->buttonUninstall->setEnabled(false);
    } else {
        ui->buttonUninstall->setEnabled(true);
    }

    if (checkUpgradable(change_list)) {
        ui->buttonInstall->setText(tr("Upgrade"));
    } else {
        ui->buttonInstall->setText(tr("Install"));
    }

    if (change_list.isEmpty()) {
        ui->buttonInstall->setEnabled(false);
        ui->buttonUninstall->setEnabled(false);
    }
}


void MainWindow::on_radioStable_toggled(bool checked)
{
    if(checked) {
        buildPackageLists();
    }
}

void MainWindow::on_radioMXtest_toggled(bool checked)
{
    if(checked) {
        buildPackageLists();
    }
}

void MainWindow::on_radioBackports_toggled(bool checked)
{
    if(checked) {
        displayWarning();
        buildPackageLists();
    }
}

// Force repo upgrade
void MainWindow::on_buttonForceUpdate_clicked()
{
    buildPackageLists(true);
}

// Hide/unhide lib/-dev packages
void MainWindow::on_checkHideLibs_clicked(bool checked)
{
    QTreeWidgetItemIterator it(ui->treeOther);
    while (*it) {
        QString app_name = (*it)->text(2);
        if (((app_name.startsWith("lib") && !app_name.startsWith("libreoffice")) || app_name.endsWith("-dev")) && checked) {
            (*it)->setHidden(true);
        } else {
            (*it)->setHidden(false);
        }
        ++it;
    }
    on_comboFilter_activated(ui->comboFilter->currentText());
}

// Upgrade all packages (from Stable repo only)
void MainWindow::on_buttonUpgradeAll_clicked()
{
    QString names;
    QTreeWidgetItemIterator it(ui->treeOther);
    QList<QTreeWidgetItem *> found_items;
    found_items = ui->treeOther->findItems("upgradable", Qt::MatchExactly, 5);

    while (*it) {
        if(found_items.contains(*it)) {
            names += (*it)->text(2) + " ";
        }
        ++it;
    }
    qDebug() << "upgrading pacakges: " << names;

    install(names);
    clearCache();
    buildPackageLists();
}
