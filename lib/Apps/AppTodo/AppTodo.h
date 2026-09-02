#pragma once
#include "BaseApp.h"
#include "../Book32_Core/InputMgr.h"
#include <vector>
#include <Arduino.h>

struct TodoItem {
    int id;
    String text;
    bool completed;
};

class AppTodo : public App {
public:
    const char* getName() override { return "Todo"; }
    const uint8_t* getIconImage() override;

    void start() override;
    void stop() override;
    void update() override;
    void draw() override;
    void forceRedraw() override;

    void handleInput(InputAction action);
    void handleTouch(uint16_t x, uint16_t y);

    // Web API methods
    std::vector<TodoItem>& getTodos() { return _todos; }
    void addTodo(const String& text);
    void toggleTodo(int id);
    void editTodo(int id, const String& text);
    void deleteTodo(int id);
    void markDirty() { _needsRedraw = true; }

private:
    std::vector<TodoItem> _todos;
    int _selectedIndex = -1;  // -1 = back button
    int _scrollOffset = 0;
    bool _needsRedraw = false;
    bool _firstDraw = true;
    bool _selectionOnlyRedraw = false;
    int _previousSelectedIndex = -1;
    int _nextId = 1;

    void loadTodos();
    void saveTodos();
    void drawTodoList();
};
