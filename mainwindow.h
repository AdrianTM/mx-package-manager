/**********************************************************************
 *  mxpackagemanager.h
 **********************************************************************
 * Copyright (C) 2017 MX Authors
 *
 * Authors: Adrian
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


#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMessageBox>
#include <QProcess>
#include <QTimer>
#include <QSettings>
#include <QFile>
#include <QDomDocument>
#include <QProgressDialog>
#include <QTreeWidgetItem>

#include <cmd.h>
#include <lockfile.h>


namespace Ui {
class MainWindow;
}

class MainWindow : public QDialog
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = 0);
    ~MainWindow();

    QString version;

    bool checkInstalled(const QString &names);    

    void displayPopularApps();
    void installPopularApp(const QString &name);
    void installPopularApps();
    void loadPmFiles();
    void processDoc(const QDomDocument &doc);
    void refreshPopularApps();
    void setProgressDialog();
    void setup();    
    void uninstall(const QString &names);
    void update();

    QString getVersion(QString name);
    QStringList listInstalled();


public slots:

private slots:
    void cleanup();
    void closeSearch();
    void cmdStart();
    void cmdDone();      
    void displayInfo(QTreeWidgetItem* item, int column);
    void findPackage();
    void setConnections();
    void tock(int, int); // tick-tock, updates progressBar when tick signal is emited
    //void updateOutput(const QString &line);

    void on_buttonInstall_clicked();
    void on_buttonAbout_clicked();
    void on_buttonHelp_clicked();
    void on_treeWidget_expanded();
    void on_treeWidget_itemClicked();
    void on_treeWidget_itemExpanded();
    void on_treeWidget_itemCollapsed();

    void on_buttonUninstall_clicked();

private:
    int heightApp;
    Cmd *cmd;
    LockFile *lock_file;
    QList<QStringList> popularApps;
    QTimer *timer;
    QProgressBar *bar;
    QProgressDialog *progress;
    QString arch;
    QStringList installedPackages;
    Ui::MainWindow *ui;

};


#endif

