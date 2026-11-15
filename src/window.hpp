#pragma once
#include <QMainWindow>
#include "transfer.hpp"
class Window : public QMainWindow {
public:
    Window();
private:
    Transfer transfer;
};
