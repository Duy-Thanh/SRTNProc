// g_processes = {
//     { L"Process 1", 2, 8, 0, 0, false },
//     { L"Process 2", 0, 4, 0, 0, false },
//     { L"Process 3", 1, 6, 0, 0, false }
// };

#include <windows.h>
#include <commctrl.h>
#include <vector>
#include <string>
#include <algorithm>
#include <thread>
#include <atomic>
#include <mutex>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "kernel32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "msimg32.lib")

// Global Variables
HWND g_hwndListView = nullptr;
HWND g_hwndStartButton = nullptr;
HWND g_hwndStopButton = nullptr;
HWND g_hwndPauseButton = nullptr;
std::atomic<bool> g_isRunning(false);
std::atomic<bool> g_isPaused(false);
std::mutex g_processMutex;

struct Process {
    std::wstring name;              // Process name
    int burstTime;                  // Burst time
    int remainingTime;              // Remaining time
    int appearingTime;              // Appearing time
    int waitingTime;                // Waiting time
    int turnaroundTime;             // Turn-around time
    bool completed;
};

// Add these to your global variables
struct ExecutionStep {
    int processIndex;  // Index of the process that was executing
    int timeUnit;      // Time unit when this execution occurred
};
std::vector<ExecutionStep> g_executionSequence;
HWND g_hwndGanttWindow = nullptr;
const int GANTT_CELL_WIDTH = 60;    // Wider cells
const int GANTT_CELL_HEIGHT = 50;   // Taller rows
const COLORREF PROCESS_COLORS[] = {
    RGB(66, 133, 244),    // Google Blue
    RGB(52, 168, 83),     // Google Green
    RGB(251, 188, 4),     // Google Yellow
    RGB(234, 67, 53),     // Google Red
    RGB(103, 58, 183)     // Material Purple
};

std::vector<Process> g_processes;

// Add these to your global variables
HWND g_hwndProcessNameEdit = nullptr;
HWND g_hwndBurstTimeEdit = nullptr;
HWND g_hwndAddProcessButton = nullptr;
HWND g_hwndAppearingTimeEdit = nullptr;
HWND g_hwndTimeUsingCPUText = nullptr;
HWND g_hwndWaitingTimeText = nullptr;
HWND g_hwndTurnaroundTimeText = nullptr;
HWND g_hwndProcessStatusText = nullptr;

// Update these global constants
const int HEADER_HEIGHT = 95;
const int TIMELINE_HEIGHT = 60;     // Taller timeline
const int PROCESS_PADDING = 12;     // Consistent padding
#define COLOR_BACKGROUND RGB(250, 250, 250)      // Off-white background
#define COLOR_TEXT RGB(60, 64, 67)              // Google Gray 900
#define COLOR_GRID RGB(241, 243, 244)           // Google Gray 100
#define COLOR_TIMELINE RGB(255, 255, 255)       // Pure white
#define COLOR_SUBTITLE RGB(95, 99, 104)         // Google Gray 700
#define COLOR_PROCESS_BG RGB(232, 240, 254)     // Light blue background

// Add these color constants for a cohesive design
const COLORREF GOOGLE_BLUE = RGB(66, 133, 244);
const COLORREF GOOGLE_GREEN = RGB(52, 168, 83);
const COLORREF GOOGLE_RED = RGB(234, 67, 53);
const COLORREF GOOGLE_YELLOW = RGB(251, 188, 4);
const COLORREF HEADER_BG = RGB(255, 255, 255);
const COLORREF HEADER_TEXT = RGB(60, 64, 67);
const COLORREF DIVIDER_COLOR = RGB(218, 220, 224);
const int HEADER_PADDING = 20;

// Add these helper functions and styles
#define ANIMATION_TIMER_ID 1001
const int ANIMATION_DURATION = 250;  // ms
const COLORREF HOVER_COLOR = RGB(232, 240, 254);  // Light blue hover
const COLORREF ACCENT_COLOR = RGB(66, 133, 244);  // Google Blue

// Add these animation and style constants
const int RIPPLE_DURATION = 300;  // ms
const int ELEVATION_ANIMATION_DURATION = 150;  // ms
const int HOVER_ELEVATION = 4;
const int RESTING_ELEVATION = 2;

// Add this helper class for smooth animations
class AnimationHelper {
public:
    static float EaseInOutQuad(float t) {
        return t < 0.5f ? 2.0f * t * t : -1.0f + (4.0f - 2.0f * t) * t;
    }

    static float EaseOutQuart(float t) {
        return 1.0f - pow(1.0f - t, 4);
    }
};

// Add this helper function before the MaterialButton class
void FillRoundRect(HDC hdc, const RECT* rect, int radius, HBRUSH brush) {
    SelectObject(hdc, brush);
    BeginPath(hdc);
    RoundRect(hdc, rect->left, rect->top, rect->right, rect->bottom, radius * 2, radius * 2);
    EndPath(hdc);
    FillPath(hdc);
}

// Forward declarations
class MaterialButton;
std::vector<MaterialButton*> g_materialButtons;

// MaterialButton class definition
class MaterialButton {
private:
    HWND m_hwnd;
    bool m_isHovered;
    bool m_isPressed;
    float m_animationProgress;
    COLORREF m_baseColor;
    COLORREF m_textColor;
    struct Ripple {
        POINT center;
        float radius;
        float progress;
        DWORD startTime;
    };
    std::vector<Ripple> m_ripples;
    float m_elevation;
    
public:
    MaterialButton(HWND hwnd, COLORREF color = ACCENT_COLOR, COLORREF textColor = RGB(255, 255, 255))
        : m_hwnd(hwnd), m_isHovered(false), m_isPressed(false), 
          m_animationProgress(0.0f), m_baseColor(color), m_textColor(textColor) {
        
        SetWindowSubclass(hwnd, ButtonProc, 0, reinterpret_cast<DWORD_PTR>(this));
    }

    ~MaterialButton() {
        RemoveWindowSubclass(m_hwnd, ButtonProc, 0);
    }

    static LRESULT CALLBACK ButtonProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
        UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
        MaterialButton* btn = reinterpret_cast<MaterialButton*>(dwRefData);
        return btn->HandleMessage(uMsg, wParam, lParam);
    }

    LRESULT HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam) {
        switch (uMsg) {
            case WM_MOUSEMOVE: {
                if (!m_isHovered) {
                    m_isHovered = true;
                    SetTimer(m_hwnd, ANIMATION_TIMER_ID, 16, nullptr);
                    TRACKMOUSEEVENT tme = { sizeof(tme) };
                    tme.dwFlags = TME_LEAVE;
                    tme.hwndTrack = m_hwnd;
                    TrackMouseEvent(&tme);
                    InvalidateRect(m_hwnd, nullptr, TRUE);
                }
                break;
            }
            case WM_MOUSELEAVE: {
                m_isHovered = false;
                m_isPressed = false;
                SetTimer(m_hwnd, ANIMATION_TIMER_ID, 16, nullptr);
                InvalidateRect(m_hwnd, nullptr, TRUE);
                break;
            }
            case WM_LBUTTONDOWN: {
                m_isPressed = true;
                POINT pt;
                GetCursorPos(&pt);
                ScreenToClient(m_hwnd, &pt);
                
                RECT rect;
                GetClientRect(m_hwnd, &rect);
                float maxRadius = sqrt(pow(rect.right, 2) + pow(rect.bottom, 2));
                
                m_ripples.push_back({
                    pt,             // center
                    maxRadius,      // radius
                    0.0f,          // progress
                    GetTickCount() // startTime
                });
                
                SetTimer(m_hwnd, ANIMATION_TIMER_ID, 16, nullptr);
                InvalidateRect(m_hwnd, nullptr, TRUE);
                break;
            }
            case WM_LBUTTONUP: {
                m_isPressed = false;
                InvalidateRect(m_hwnd, nullptr, TRUE);
                break;
            }
            case WM_TIMER: {
                if (wParam == ANIMATION_TIMER_ID) {
                    float targetProgress = m_isHovered ? 1.0f : 0.0f;
                    float step = 0.1f;
                    
                    if (abs(m_animationProgress - targetProgress) < step) {
                        m_animationProgress = targetProgress;
                        KillTimer(m_hwnd, ANIMATION_TIMER_ID);
                    } else {
                        m_animationProgress += (targetProgress - m_animationProgress) * step;
                    }
                    
                    InvalidateRect(m_hwnd, nullptr, TRUE);
                }
                break;
            }
            case WM_PAINT: {
                PAINTSTRUCT ps;
                HDC hdc = BeginPaint(m_hwnd, &ps);
                RECT rect;
                GetClientRect(m_hwnd, &rect);

                // Create memory DC for double buffering
                HDC memDC = CreateCompatibleDC(hdc);
                HBITMAP memBitmap = CreateCompatibleBitmap(hdc, rect.right, rect.bottom);
                SelectObject(memDC, memBitmap);

                // Draw shadow based on elevation
                if (m_elevation > 0) {
                    for (int i = 0; i < (int)m_elevation; i++) {
                        RECT shadowRect = rect;
                        InflateRect(&shadowRect, -i, -i);
                        HBRUSH shadowBrush = CreateSolidBrush(RGB(0, 0, 0, 32 / (i + 1)));
                        FillRoundRect(memDC, &shadowRect, 4, shadowBrush);
                        DeleteObject(shadowBrush);
                    }
                }

                // Draw button background
                COLORREF bgColor = m_baseColor;
                if (m_isHovered || m_isPressed) {
                    bgColor = RGB(
                        GetRValue(bgColor) + (255 - GetRValue(bgColor)) * m_animationProgress * 0.2f,
                        GetGValue(bgColor) + (255 - GetGValue(bgColor)) * m_animationProgress * 0.2f,
                        GetBValue(bgColor) + (255 - GetBValue(bgColor)) * m_animationProgress * 0.2f
                    );
                }

                HBRUSH bgBrush = CreateSolidBrush(bgColor);
                FillRoundRect(memDC, &rect, 4, bgBrush);
                DeleteObject(bgBrush);

                // Draw ripples
                SetBkMode(memDC, TRANSPARENT);
                for (const auto& ripple : m_ripples) {
                    float alpha = 1.0f - ripple.progress;
                    COLORREF rippleColor = RGB(255, 255, 255);
                    HBRUSH rippleBrush = CreateSolidBrush(rippleColor);
                    HPEN ripplePen = CreatePen(PS_SOLID, 1, rippleColor);
                    SelectObject(memDC, rippleBrush);
                    SelectObject(memDC, ripplePen);

                    BeginPath(memDC);
                    Ellipse(memDC, 
                        ripple.center.x - ripple.radius * ripple.progress,
                        ripple.center.y - ripple.radius * ripple.progress,
                        ripple.center.x + ripple.radius * ripple.progress,
                        ripple.center.y + ripple.radius * ripple.progress);
                    EndPath(memDC);
                    FillPath(memDC);

                    DeleteObject(rippleBrush);
                    DeleteObject(ripplePen);
                }

                // Draw text with better anti-aliasing
                SetTextColor(memDC, m_textColor);
                HFONT hFont = CreateFont(14, 0, 0, 0, FW_MEDIUM, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                    CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
                SelectObject(memDC, hFont);

                wchar_t text[256];
                GetWindowText(m_hwnd, text, 256);
                DrawText(memDC, text, -1, &rect, DT_SINGLELINE | DT_CENTER | DT_VCENTER);

                // Copy to screen
                BitBlt(hdc, 0, 0, rect.right, rect.bottom, memDC, 0, 0, SRCCOPY);

                // Cleanup
                DeleteObject(hFont);
                DeleteObject(memBitmap);
                DeleteDC(memDC);
                EndPaint(m_hwnd, &ps);
                return 0;
            }
        }
        return DefSubclassProc(m_hwnd, uMsg, wParam, lParam);
    }
};

LRESULT CALLBACK GanttWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        // Enable anti-aliasing
        SetBkMode(hdc, TRANSPARENT);
        SetTextAlign(hdc, TA_LEFT | TA_TOP);

        // Create modern fonts
        HFONT hTitleFont = CreateFont(36, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, VARIABLE_PITCH | FF_SWISS, L"Segoe UI");
        HFONT hSubtitleFont = CreateFont(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, VARIABLE_PITCH | FF_SWISS, L"Segoe UI");
        HFONT hLabelFont = CreateFont(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, VARIABLE_PITCH | FF_SWISS, L"Segoe UI");

        // Fill background
        RECT clientRect;
        GetClientRect(hwnd, &clientRect);
        FillRect(hdc, &clientRect, CreateSolidBrush(COLOR_BACKGROUND));

        // Draw header with subtle shadow
        RECT headerRect = { 0, 0, clientRect.right, HEADER_HEIGHT };
        FillRect(hdc, &headerRect, CreateSolidBrush(COLOR_TIMELINE));
        
        // Draw header shadow
        HPEN shadowPen = CreatePen(PS_SOLID, 1, RGB(0, 0, 0, 16));
        SelectObject(hdc, shadowPen);
        MoveToEx(hdc, 0, HEADER_HEIGHT, NULL);
        LineTo(hdc, clientRect.right, HEADER_HEIGHT);
        DeleteObject(shadowPen);

        // Draw title and subtitle with better spacing
        SelectObject(hdc, hTitleFont);
        SetTextColor(hdc, COLOR_TEXT);
        TextOut(hdc, 40, 25, L"Process Timeline", 15);

        SelectObject(hdc, hSubtitleFont);
        SetTextColor(hdc, COLOR_SUBTITLE);
        TextOut(hdc, 42, 65, L"Shortest Remaining Time Next", 26);

        int maxTime = g_executionSequence.empty() ? 0 : g_executionSequence.back().timeUnit + 1;
        int xOffset = 200;  // Increased space for process names
        int yOffset = HEADER_HEIGHT + TIMELINE_HEIGHT;

        // Draw timeline background
        RECT timelineRect = { xOffset, HEADER_HEIGHT, 
                            xOffset + (maxTime + 1) * GANTT_CELL_WIDTH, 
                            HEADER_HEIGHT + TIMELINE_HEIGHT };
        FillRect(hdc, &timelineRect, CreateSolidBrush(RGB(255, 255, 255)));

        // Switch to label font for remaining text
        SelectObject(hdc, hLabelFont);

        // Draw process rows
        for (size_t i = 0; i < g_processes.size(); i++) {
            // Process row background
            RECT rowRect = {
                0,
                yOffset + i * GANTT_CELL_HEIGHT,
                clientRect.right,
                yOffset + (i + 1) * GANTT_CELL_HEIGHT
            };
            FillRect(hdc, &rowRect, CreateSolidBrush(i % 2 == 0 ? RGB(255, 255, 255) : RGB(252, 252, 252)));

            // Draw process name with pill background
            RECT nameRect = {
                30,
                yOffset + i * GANTT_CELL_HEIGHT + PROCESS_PADDING,
                xOffset - 30,
                yOffset + (i + 1) * GANTT_CELL_HEIGHT - PROCESS_PADDING
            };

            // Draw modern pill background
            HBRUSH pillBrush = CreateSolidBrush(COLOR_PROCESS_BG);
            SelectObject(hdc, pillBrush);
            BeginPath(hdc);
            RoundRect(hdc, nameRect.left, nameRect.top, nameRect.right, nameRect.bottom, 25, 25);
            EndPath(hdc);
            FillPath(hdc);
            DeleteObject(pillBrush);

            // Draw process blocks with subtle shadow
            for (size_t j = 0; j < g_executionSequence.size(); j++) {
                if (g_executionSequence[j].processIndex == i) {
                    RECT blockRect = {
                        xOffset + g_executionSequence[j].timeUnit * GANTT_CELL_WIDTH + 4,
                        yOffset + i * GANTT_CELL_HEIGHT + PROCESS_PADDING,
                        xOffset + (g_executionSequence[j].timeUnit + 1) * GANTT_CELL_WIDTH - 4,
                        yOffset + (i + 1) * GANTT_CELL_HEIGHT - PROCESS_PADDING
                    };

                    // Draw block with smooth corners
                    HBRUSH blockBrush = CreateSolidBrush(PROCESS_COLORS[i]);
                    FillRoundRect(hdc, &blockRect, 8, blockBrush);
                    
                    // Add subtle border
                    HPEN borderPen = CreatePen(PS_SOLID, 1, 
                        RGB(GetRValue(PROCESS_COLORS[i]) - 30,
                            GetGValue(PROCESS_COLORS[i]) - 30,
                            GetBValue(PROCESS_COLORS[i]) - 30));
                    SelectObject(hdc, borderPen);
                    RoundRect(hdc, blockRect.left, blockRect.top, 
                             blockRect.right, blockRect.bottom, 16, 16);
                    
                    // Cleanup
                    DeleteObject(blockBrush);
                    DeleteObject(borderPen);
                }
            }

            // Draw process name
            SetTextColor(hdc, COLOR_TEXT);  // Ensure text color is set
            DrawText(hdc, g_processes[i].name.c_str(), -1, &nameRect, 
                    DT_SINGLELINE | DT_VCENTER | DT_CENTER);
        }

        // Draw timeline markers with explicit color
        for (int t = 0; t <= maxTime; t++) {
            // Vertical grid lines
            HPEN gridPen = CreatePen(PS_SOLID, 1, COLOR_GRID);
            SelectObject(hdc, gridPen);
            MoveToEx(hdc, xOffset + t * GANTT_CELL_WIDTH, HEADER_HEIGHT, NULL);
            LineTo(hdc, xOffset + t * GANTT_CELL_WIDTH, clientRect.bottom - 20);
            DeleteObject(gridPen);

            // Time markers with explicit color
            SetTextColor(hdc, COLOR_TEXT);  // Reset text color before each text draw
            WCHAR timeStr[8];
            swprintf_s(timeStr, L"%d", t);
            TextOut(hdc, xOffset + t * GANTT_CELL_WIDTH - 5, 
                   HEADER_HEIGHT + (TIMELINE_HEIGHT - 20) / 2, timeStr, wcslen(timeStr));
        }

        // Cleanup
        DeleteObject(hTitleFont);
        DeleteObject(hSubtitleFont);
        DeleteObject(hLabelFont);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_DESTROY:
        g_hwndGanttWindow = nullptr;
        return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

void ShowGanttChart(HWND hwndParent) {
    if (g_hwndGanttWindow != nullptr) {
        return;  // Already showing
    }

    // Register window class for Gantt chart
    static bool registered = false;
    if (!registered) {
        WNDCLASS wc = { 0 };
        wc.lpfnWndProc = GanttWindowProc;
        wc.hInstance = GetModuleHandle(NULL);
        wc.lpszClassName = L"GanttChartWindow";
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        RegisterClass(&wc);
        registered = true;
    }

    // Calculate window size with better proportions
    int maxTime = g_executionSequence.empty() ? 0 : g_executionSequence.back().timeUnit + 1;
    int width = 200 + (maxTime + 1) * GANTT_CELL_WIDTH + 40;  // Reduced padding
    int height = HEADER_HEIGHT + TIMELINE_HEIGHT + 
                 g_processes.size() * GANTT_CELL_HEIGHT + 40;  // Reduced padding

    // Center on parent
    RECT parentRect;
    GetWindowRect(hwndParent, &parentRect);
    int x = parentRect.left + (parentRect.right - parentRect.left - width) / 2;
    int y = parentRect.top + (parentRect.bottom - parentRect.top - height) / 2;

    // Create window with modern style and shadow
    g_hwndGanttWindow = CreateWindowEx(
        WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        L"GanttChartWindow",
        L"Process Timeline",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        x, y, width, height,
        hwndParent, NULL, GetModuleHandle(NULL), NULL);

    ShowWindow(g_hwndGanttWindow, SW_SHOW);
    UpdateWindow(g_hwndGanttWindow);
}

// Helper function to set text in ListView
void SetListViewText(int row, int col, const std::wstring& text) {
    LVITEM lvi = { 0 };
    lvi.mask = LVIF_TEXT;
    lvi.iItem = row;
    lvi.iSubItem = col;
    lvi.pszText = const_cast<LPWSTR>(text.c_str());
    if (col == 0) {
        ListView_InsertItem(g_hwndListView, &lvi);
    }
    else {
        ListView_SetItem(g_hwndListView, &lvi);
    }
}

// Update ListView with current process states
void UpdateListView() {
    ListView_DeleteAllItems(g_hwndListView);

    std::lock_guard<std::mutex> lock(g_processMutex);
    for (size_t i = 0; i < g_processes.size(); i++) {
        // Process number
        SetListViewText(i, 0, std::to_wstring(i + 1));

        // Process name
        SetListViewText(i, 1, g_processes[i].name);

        // Remaining time
        SetListViewText(i, 2, std::to_wstring(g_processes[i].remainingTime));

        // Burst time
        SetListViewText(i, 3, std::to_wstring(g_processes[i].burstTime));

        // Waiting time
        SetListViewText(i, 4, std::to_wstring(g_processes[i].waitingTime));

        // Turnaround time
        SetListViewText(i, 5, std::to_wstring(g_processes[i].turnaroundTime));

        // Status
        SetListViewText(i, 6, g_processes[i].completed ? L"Completed" : (g_processes[i].remainingTime == g_processes[i].burstTime ? L"Waiting" : L"Running"));
    }
}

// Scheduler algorithm implementation
void SchedulerThread() {
    while (g_isRunning) {
        if (!g_isPaused) {
            std::lock_guard<std::mutex> lock(g_processMutex);

            // Check if all processes are completed
            bool allCompleted = std::all_of(g_processes.begin(), g_processes.end(),
                [](const Process& p) { return p.completed; });

            if (allCompleted) {
                g_isRunning = false;
                PostMessage(GetParent(g_hwndListView), WM_COMMAND, 1000, 0); // Notify completion
                break;
            }

            // Find process with shortest remaining time
            auto shortestProcess = std::min_element(g_processes.begin(), g_processes.end(),
                [](const Process& a, const Process& b) {
                    if (a.completed) return false;
                    if (b.completed) return true;
                    return a.remainingTime < b.remainingTime;
                });

            if (shortestProcess != g_processes.end() && !shortestProcess->completed) {
                g_executionSequence.push_back({
                    static_cast<int>(shortestProcess - g_processes.begin()),
                    static_cast<int>(g_executionSequence.size())
                });
                // Execute process for one time unit
                shortestProcess->remainingTime--;

                // Update waiting times for other processes
                for (auto& process : g_processes) {
                    if (!process.completed && &process != &(*shortestProcess)) {
                        process.waitingTime++;
                    }
                }

                // Check if process completed
                if (shortestProcess->remainingTime == 0) {
                    shortestProcess->completed = true;
                    shortestProcess->turnaroundTime = shortestProcess->waitingTime + shortestProcess->burstTime;
                }

                // Request UI update
                PostMessage(GetParent(g_hwndListView), WM_COMMAND, 999, 0);
            }
        }
        Sleep(1000); // 1 second time unit
    }
}

// Initialize ListView columns
void InitializeListView(HWND hwndListView) {
    LVCOLUMN lvc = { 0 };
    lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;

    struct ColumnInfo {
        const wchar_t* name;
        int width;
    } columns[] = {
        { L"No.", 50 },
        { L"Process Name", 120 },
        { L"Time using CPU", 120 },
        { L"Appearing Time", 120 },
        { L"Waiting Time", 120 },
        { L"Turnaround Time", 150 },
        { L"Process Status", 120 }
    };

    for (int i = 0; i < _countof(columns); i++) {
        lvc.pszText = const_cast<LPWSTR>(columns[i].name);
        lvc.cx = columns[i].width;
        ListView_InsertColumn(hwndListView, i, &lvc);
    }

    // Set extended ListView styles for a modern look
    ListView_SetExtendedListViewStyle(hwndListView,
        LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);

    // Set custom colors for the ListView
    ListView_SetBkColor(hwndListView, RGB(255, 255, 255));
    ListView_SetTextBkColor(hwndListView, RGB(255, 255, 255));
    ListView_SetTextColor(hwndListView, RGB(0, 0, 0));
}

// Add this modern input field class
class MaterialTextField {
private:
    HWND m_hwnd;
    bool m_isFocused;
    bool m_isHovered;
    float m_focusProgress;
    std::wstring m_placeholder;
    COLORREF m_accentColor;

public:
    MaterialTextField(HWND hwnd, const std::wstring& placeholder, COLORREF accent = GOOGLE_BLUE)
        : m_hwnd(hwnd), m_isFocused(false), m_isHovered(false), 
          m_focusProgress(0.0f), m_placeholder(placeholder), m_accentColor(accent) {
        
        SetWindowSubclass(hwnd, TextFieldProc, 0, reinterpret_cast<DWORD_PTR>(this));
    }

    static LRESULT CALLBACK TextFieldProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
        UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
        MaterialTextField* tf = reinterpret_cast<MaterialTextField*>(dwRefData);
        return tf->HandleMessage(uMsg, wParam, lParam);
    }

    LRESULT HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam) {
        switch (uMsg) {
            case WM_PAINT: {
                PAINTSTRUCT ps;
                HDC hdc = BeginPaint(m_hwnd, &ps);
                RECT rect;
                GetClientRect(m_hwnd, &rect);

                // Create memory DC
                HDC memDC = CreateCompatibleDC(hdc);
                HBITMAP memBitmap = CreateCompatibleBitmap(hdc, rect.right, rect.bottom);
                SelectObject(memDC, memBitmap);

                // Fill background
                HBRUSH bgBrush = CreateSolidBrush(RGB(255, 255, 255));
                FillRect(memDC, &rect, bgBrush);
                DeleteObject(bgBrush);

                // Draw underline
                int underlineY = rect.bottom - 2;
                COLORREF lineColor = m_isFocused ? m_accentColor : RGB(200, 200, 200);
                if (m_isHovered && !m_isFocused) {
                    lineColor = RGB(150, 150, 150);
                }

                HPEN linePen = CreatePen(PS_SOLID, 2, lineColor);
                SelectObject(memDC, linePen);
                MoveToEx(memDC, rect.left, underlineY, NULL);
                LineTo(memDC, rect.right, underlineY);
                DeleteObject(linePen);

                // Draw text
                SetBkMode(memDC, TRANSPARENT);
                HFONT hFont = CreateFont(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                    CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
                SelectObject(memDC, hFont);

                wchar_t text[256];
                GetWindowText(m_hwnd, text, 256);
                
                if (wcslen(text) == 0 && !m_isFocused) {
                    // Draw placeholder
                    SetTextColor(memDC, RGB(150, 150, 150));
                    DrawText(memDC, m_placeholder.c_str(), -1, &rect, 
                            DT_SINGLELINE | DT_VCENTER | DT_LEFT);
                } else {
                    // Draw actual text
                    SetTextColor(memDC, RGB(0, 0, 0));
                    DrawText(memDC, text, -1, &rect, 
                            DT_SINGLELINE | DT_VCENTER | DT_LEFT);
                }

                // Copy to screen
                BitBlt(hdc, 0, 0, rect.right, rect.bottom, memDC, 0, 0, SRCCOPY);

                // Cleanup
                DeleteObject(hFont);
                DeleteObject(memBitmap);
                DeleteDC(memDC);
                EndPaint(m_hwnd, &ps);
                return 0;
            }

            case WM_SETFOCUS: {
                m_isFocused = true;
                SetTimer(m_hwnd, ANIMATION_TIMER_ID, 16, nullptr);
                InvalidateRect(m_hwnd, nullptr, TRUE);
                break;
            }

            case WM_KILLFOCUS: {
                m_isFocused = false;
                SetTimer(m_hwnd, ANIMATION_TIMER_ID, 16, nullptr);
                InvalidateRect(m_hwnd, nullptr, TRUE);
                break;
            }

            case WM_MOUSEMOVE: {
                if (!m_isHovered) {
                    m_isHovered = true;
                    TRACKMOUSEEVENT tme = { sizeof(tme) };
                    tme.dwFlags = TME_LEAVE;
                    tme.hwndTrack = m_hwnd;
                    TrackMouseEvent(&tme);
                    InvalidateRect(m_hwnd, nullptr, TRUE);
                }
                break;
            }

            case WM_MOUSELEAVE: {
                m_isHovered = false;
                InvalidateRect(m_hwnd, nullptr, TRUE);
                break;
            }
        }
        return DefSubclassProc(m_hwnd, uMsg, wParam, lParam);
    }
};

// Add with other global declarations
std::vector<MaterialTextField*> g_textFields;

// Add these new style constants
const COLORREF STATUS_CARD_BG = RGB(255, 255, 255);
const COLORREF STATUS_CARD_BORDER = RGB(218, 220, 224);
const COLORREF STATUS_RUNNING = RGB(52, 168, 83);    // Google Green
const COLORREF STATUS_WAITING = RGB(251, 188, 4);    // Google Yellow
const COLORREF STATUS_COMPLETED = RGB(66, 133, 244); // Google Blue

// Add this modern progress indicator class
class CircularProgress {
private:
    HWND m_hwnd;
    float m_progress;
    float m_targetProgress;
    COLORREF m_color;
    bool m_isIndeterminate;
    float m_rotationAngle;

public:
    CircularProgress(HWND hwnd, COLORREF color = GOOGLE_BLUE)
        : m_hwnd(hwnd), m_progress(0.0f), m_targetProgress(0.0f),
          m_color(color), m_isIndeterminate(false), m_rotationAngle(0.0f) {
        SetWindowSubclass(hwnd, ProgressProc, 0, reinterpret_cast<DWORD_PTR>(this));
        SetTimer(m_hwnd, ANIMATION_TIMER_ID, 16, nullptr);
    }

    static LRESULT CALLBACK ProgressProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
        UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
        CircularProgress* cp = reinterpret_cast<CircularProgress*>(dwRefData);
        return cp->HandleMessage(uMsg, wParam, lParam);
    }

    void SetProgress(float progress) {
        m_targetProgress = progress;
        m_isIndeterminate = false;
        InvalidateRect(m_hwnd, nullptr, TRUE);
    }

    void SetIndeterminate(bool indeterminate) {
        m_isIndeterminate = indeterminate;
        InvalidateRect(m_hwnd, nullptr, TRUE);
    }

    LRESULT HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam) {
        switch (uMsg) {
            case WM_PAINT: {
                PAINTSTRUCT ps;
                HDC hdc = BeginPaint(m_hwnd, &ps);
                RECT rect;
                GetClientRect(m_hwnd, &rect);

                // Create memory DC
                HDC memDC = CreateCompatibleDC(hdc);
                HBITMAP memBitmap = CreateCompatibleBitmap(hdc, rect.right, rect.bottom);
                SelectObject(memDC, memBitmap);

                // Clear background
                HBRUSH bgBrush = CreateSolidBrush(RGB(255, 255, 255));
                FillRect(memDC, &rect, bgBrush);
                DeleteObject(bgBrush);

                // Calculate center and radius
                int centerX = rect.right / 2;
                int centerY = rect.bottom / 2;
                int radius = min(centerX, centerY) - 4;

                // Enable anti-aliasing
                SetBkMode(memDC, TRANSPARENT);
                
                if (m_isIndeterminate) {
                    // Draw rotating arc
                    HPEN arcPen = CreatePen(PS_SOLID, 3, m_color);
                    SelectObject(memDC, arcPen);
                    
                    float startAngle = m_rotationAngle;
                    float sweepAngle = 270.0f;
                    
                    BeginPath(memDC);
                    Arc(memDC,
                        centerX - radius, centerY - radius,
                        centerX + radius, centerY + radius,
                        centerX + radius * cos(startAngle),
                        centerY + radius * sin(startAngle),
                        centerX + radius * cos(startAngle + sweepAngle * 3.14159f / 180.0f),
                        centerY + radius * sin(startAngle + sweepAngle * 3.14159f / 180.0f));
                    EndPath(memDC);
                    StrokePath(memDC);
                    
                    DeleteObject(arcPen);
                } else {
                    // Draw progress arc
                    HPEN progressPen = CreatePen(PS_SOLID, 3, m_color);
                    SelectObject(memDC, progressPen);
                    
                    float sweepAngle = m_progress * 360.0f;
                    
                    BeginPath(memDC);
                    Arc(memDC,
                        centerX - radius, centerY - radius,
                        centerX + radius, centerY + radius,
                        centerX + radius, centerY,
                        centerX + radius * cos(sweepAngle * 3.14159f / 180.0f),
                        centerY + radius * sin(sweepAngle * 3.14159f / 180.0f));
                    EndPath(memDC);
                    StrokePath(memDC);
                    
                    DeleteObject(progressPen);
                }

                // Copy to screen
                BitBlt(hdc, 0, 0, rect.right, rect.bottom, memDC, 0, 0, SRCCOPY);

                // Cleanup
                DeleteObject(memBitmap);
                DeleteDC(memDC);
                EndPaint(m_hwnd, &ps);
                return 0;
            }

            case WM_TIMER: {
                if (wParam == ANIMATION_TIMER_ID) {
                    if (m_isIndeterminate) {
                        m_rotationAngle += 0.1f;
                        if (m_rotationAngle >= 6.28318f) {
                            m_rotationAngle = 0.0f;
                        }
                    } else {
                        float diff = m_targetProgress - m_progress;
                        if (abs(diff) > 0.01f) {
                            m_progress += diff * 0.1f;
                            InvalidateRect(m_hwnd, nullptr, TRUE);
                        }
                    }
                    InvalidateRect(m_hwnd, nullptr, TRUE);
                }
                break;
            }
        }
        return DefSubclassProc(m_hwnd, uMsg, wParam, lParam);
    }
};

// Add this vector to your global variables
std::vector<CircularProgress*> g_progressIndicators;

// Add these style constants
const int CARD_PADDING = 16;
const int CARD_RADIUS = 8;
const int CARD_ELEVATION = 2;
const COLORREF CARD_SHADOW = RGB(0, 0, 0);
const int PROCESS_CARD_HEIGHT = 80;

// Add this modern card class
class ProcessCard {
private:
    HWND m_hwnd;
    Process* m_process;
    float m_elevationProgress;
    bool m_isHovered;

public:
    ProcessCard(HWND hwnd, Process* process)
        : m_hwnd(hwnd), m_process(process), 
          m_elevationProgress(0.0f), m_isHovered(false) {
        SetWindowSubclass(hwnd, CardProc, 0, reinterpret_cast<DWORD_PTR>(this));
    }

    static LRESULT CALLBACK CardProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
        UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
        ProcessCard* card = reinterpret_cast<ProcessCard*>(dwRefData);
        return card->HandleMessage(uMsg, wParam, lParam);
    }

    LRESULT HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam) {
        switch (uMsg) {
            case WM_PAINT: {
                PAINTSTRUCT ps;
                HDC hdc = BeginPaint(m_hwnd, &ps);
                RECT rect;
                GetClientRect(m_hwnd, &rect);

                // Create memory DC
                HDC memDC = CreateCompatibleDC(hdc);
                HBITMAP memBitmap = CreateCompatibleBitmap(hdc, rect.right, rect.bottom);
                SelectObject(memDC, memBitmap);

                // Draw shadow
                float elevation = CARD_ELEVATION + (m_isHovered ? 2.0f : 0.0f) * m_elevationProgress;
                for (int i = 0; i < (int)elevation; i++) {
                    RECT shadowRect = rect;
                    InflateRect(&shadowRect, -i, -i);
                    HBRUSH shadowBrush = CreateSolidBrush(RGB(0, 0, 0, 32 / (i + 1)));
                    FillRoundRect(memDC, &shadowRect, CARD_RADIUS, shadowBrush);
                    DeleteObject(shadowBrush);
                }

                // Draw card background
                RECT cardRect = rect;
                InflateRect(&cardRect, -CARD_ELEVATION, -CARD_ELEVATION);
                HBRUSH cardBrush = CreateSolidBrush(STATUS_CARD_BG);
                FillRoundRect(memDC, &cardRect, CARD_RADIUS, cardBrush);
                DeleteObject(cardBrush);

                // Draw process status indicator
                COLORREF statusColor;
                const wchar_t* statusText;
                if (m_process->completed) {
                    statusColor = STATUS_COMPLETED;
                    statusText = L"Completed";
                } else if (m_process->remainingTime < m_process->burstTime) {
                    statusColor = STATUS_RUNNING;
                    statusText = L"Running";
                } else {
                    statusColor = STATUS_WAITING;
                    statusText = L"Waiting";
                }

                // Draw status pill
                RECT statusRect = { 
                    cardRect.left + CARD_PADDING, 
                    cardRect.top + CARD_PADDING,
                    cardRect.left + CARD_PADDING + 80,
                    cardRect.top + CARD_PADDING + 24
                };
                HBRUSH statusBrush = CreateSolidBrush(statusColor);
                FillRoundRect(memDC, &statusRect, 12, statusBrush);
                DeleteObject(statusBrush);

                // Draw status text
                SetBkMode(memDC, TRANSPARENT);
                SetTextColor(memDC, RGB(255, 255, 255));
                HFONT statusFont = CreateFont(12, 0, 0, 0, FW_MEDIUM, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                    CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
                SelectObject(memDC, statusFont);
                DrawText(memDC, statusText, -1, &statusRect, 
                        DT_SINGLELINE | DT_CENTER | DT_VCENTER);
                DeleteObject(statusFont);

                // Draw process info
                SetTextColor(memDC, RGB(0, 0, 0));
                HFONT infoFont = CreateFont(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                    CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
                SelectObject(memDC, infoFont);

                // Process name
                RECT nameRect = cardRect;
                nameRect.left += CARD_PADDING;
                nameRect.top += CARD_PADDING + 30;
                DrawText(memDC, m_process->name.c_str(), -1, &nameRect, DT_SINGLELINE);

                // Progress bar background
                RECT progressRect = {
                    cardRect.left + CARD_PADDING,
                    cardRect.bottom - CARD_PADDING - 8,
                    cardRect.right - CARD_PADDING,
                    cardRect.bottom - CARD_PADDING
                };
                HBRUSH progressBgBrush = CreateSolidBrush(RGB(238, 238, 238));
                FillRoundRect(memDC, &progressRect, 4, progressBgBrush);
                DeleteObject(progressBgBrush);

                // Progress bar fill
                float progress = 1.0f - (float)m_process->remainingTime / m_process->burstTime;
                RECT fillRect = progressRect;
                fillRect.right = fillRect.left + (fillRect.right - fillRect.left) * progress;
                HBRUSH progressFillBrush = CreateSolidBrush(statusColor);
                FillRoundRect(memDC, &fillRect, 4, progressFillBrush);
                DeleteObject(progressFillBrush);

                // Copy to screen
                BitBlt(hdc, 0, 0, rect.right, rect.bottom, memDC, 0, 0, SRCCOPY);

                // Cleanup
                DeleteObject(infoFont);
                DeleteObject(memBitmap);
                DeleteDC(memDC);
                EndPaint(m_hwnd, &ps);
                return 0;
            }

            case WM_MOUSEMOVE: {
                if (!m_isHovered) {
                    m_isHovered = true;
                    SetTimer(m_hwnd, ANIMATION_TIMER_ID, 16, nullptr);
                    TRACKMOUSEEVENT tme = { sizeof(tme) };
                    tme.dwFlags = TME_LEAVE;
                    tme.hwndTrack = m_hwnd;
                    TrackMouseEvent(&tme);
                    InvalidateRect(m_hwnd, nullptr, TRUE);
                }
                break;
            }

            case WM_MOUSELEAVE: {
                m_isHovered = false;
                SetTimer(m_hwnd, ANIMATION_TIMER_ID, 16, nullptr);
                InvalidateRect(m_hwnd, nullptr, TRUE);
                break;
            }

            case WM_TIMER: {
                if (wParam == ANIMATION_TIMER_ID) {
                    float targetProgress = m_isHovered ? 1.0f : 0.0f;
                    float step = 0.1f;
                    
                    if (abs(m_elevationProgress - targetProgress) < step) {
                        m_elevationProgress = targetProgress;
                        KillTimer(m_hwnd, ANIMATION_TIMER_ID);
                    } else {
                        m_elevationProgress += (targetProgress - m_elevationProgress) * step;
                    }
                    
                    InvalidateRect(m_hwnd, nullptr, TRUE);
                }
                break;
            }
        }
        return DefSubclassProc(m_hwnd, uMsg, wParam, lParam);
    }
};

// Add this vector to your global variables
std::vector<ProcessCard*> g_processCards;

// Window Procedure
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE: {
        // Create a modern white header section with subtle shadow
        RECT headerRect = { 0, 0, 850, HEADER_HEIGHT };
        FillRect(GetDC(hwnd), &headerRect, CreateSolidBrush(HEADER_BG));
        
        // Draw header shadow
        HPEN shadowPen = CreatePen(PS_SOLID, 1, RGB(0, 0, 0, 16));
        HDC hdc = GetDC(hwnd);
        SelectObject(hdc, shadowPen);
        MoveToEx(hdc, 0, HEADER_HEIGHT, NULL);
        LineTo(hdc, 850, HEADER_HEIGHT);
        DeleteObject(shadowPen);

        // Control buttons with Material Design style
        g_hwndStartButton = CreateWindow(
            L"BUTTON", L"Start",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            HEADER_PADDING, 25, 80, 32,  // Consistent height of 32
            hwnd, (HMENU)1, NULL, NULL);

        g_hwndPauseButton = CreateWindow(
            L"BUTTON", L"Pause",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            110, 25, 80, 32,
            hwnd, (HMENU)2, NULL, NULL);

        g_hwndStopButton = CreateWindow(
            L"BUTTON", L"Stop",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            200, 25, 80, 32,
            hwnd, (HMENU)3, NULL, NULL);

        // Modern input fields with floating labels
        CreateWindow(
            L"STATIC", L"Process",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            360, 13, 100, 15,
            hwnd, NULL, NULL, NULL);

        g_hwndProcessNameEdit = CreateWindowEx(
            WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            360, 28, 100, 23,
            hwnd, NULL, NULL, NULL);

        CreateWindow(
            L"STATIC", L"Appearing",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            540, 13, 60, 15,
            hwnd, NULL, NULL, NULL);

        g_hwndAppearingTimeEdit = CreateWindowEx(
            WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_NUMBER,
            540, 28, 60, 23,
            hwnd, NULL, NULL, NULL);

        CreateWindow(
            L"STATIC", L"Burst",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            660, 13, 60, 15,
            hwnd, NULL, NULL, NULL);

        g_hwndBurstTimeEdit = CreateWindowEx(
            WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_NUMBER,
            660, 28, 60, 23,
            hwnd, NULL, NULL, NULL);

        g_hwndAddProcessButton = CreateWindow(
            L"BUTTON", L"Add Process",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            730, 25, 90, 32,
            hwnd, (HMENU)4, NULL, NULL);

        // Modern ListView with better styling
        g_hwndListView = CreateWindowEx(
            0, WC_LISTVIEW, L"",
            WS_CHILD | WS_VISIBLE | LVS_REPORT | WS_BORDER,
            10, HEADER_HEIGHT + 10, 830, 275,
            hwnd, (HMENU)5, NULL, NULL);

        // Set modern font for all controls
        HFONT hFont = CreateFont(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

        EnumChildWindows(hwnd, [](HWND hwndChild, LPARAM lParam) {
            SendMessage(hwndChild, WM_SETFONT, (WPARAM)lParam, TRUE);
            return TRUE;
        }, (LPARAM)hFont);

        // Initialize ListView with modern style
        ListView_SetExtendedListViewStyle(g_hwndListView, 
            LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_GRIDLINES);

        // Add ListView columns with better spacing
        LVCOLUMN lvc = { 0 };
        lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        
        wchar_t colNo[] = L"No.";
        lvc.iSubItem = 0;
        lvc.cx = 50;
        lvc.pszText = colNo;
        ListView_InsertColumn(g_hwndListView, 0, &lvc);

        wchar_t colName[] = L"Process Name";
        lvc.iSubItem = 1;
        lvc.cx = 120;
        lvc.pszText = colName;
        ListView_InsertColumn(g_hwndListView, 1, &lvc);

        wchar_t colCPU[] = L"Time using CPU";
        lvc.iSubItem = 2;
        lvc.cx = 100;
        lvc.pszText = colCPU;
        ListView_InsertColumn(g_hwndListView, 2, &lvc);

        wchar_t colAppearing[] = L"Appearing Time";
        lvc.iSubItem = 3;
        lvc.cx = 100;
        lvc.pszText = colAppearing;
        ListView_InsertColumn(g_hwndListView, 3, &lvc);

        wchar_t colWaiting[] = L"Waiting Time";
        lvc.iSubItem = 4;
        lvc.cx = 100;
        lvc.pszText = colWaiting;
        ListView_InsertColumn(g_hwndListView, 4, &lvc);

        wchar_t colTurnaround[] = L"Turnaround Time";
        lvc.iSubItem = 5;
        lvc.cx = 110;
        lvc.pszText = colTurnaround;
        ListView_InsertColumn(g_hwndListView, 5, &lvc);

        wchar_t colStatus[] = L"Status";
        lvc.iSubItem = 6;
        lvc.cx = 100;
        lvc.pszText = colStatus;
        ListView_InsertColumn(g_hwndListView, 6, &lvc);

        return 0;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case 1: // Start button
            if (!g_isRunning) {
                // Remove the hard-coded process initialization
                g_executionSequence.clear();

                // Validate that we have at least one process
                if (g_processes.empty()) {
                    MessageBox(hwnd, L"Please add at least one process before starting.", 
                             L"No Processes", MB_OK | MB_ICONWARNING);
                    return 0;
                }

                g_isRunning = true;
                g_isPaused = false;

                // Start scheduler thread
                std::thread(SchedulerThread).detach();

                EnableWindow(g_hwndStartButton, FALSE);
                EnableWindow(g_hwndPauseButton, TRUE);
                EnableWindow(g_hwndStopButton, TRUE);

                UpdateListView();
            }
            break;

        case 2: // Pause button
            if (g_isRunning) {
                g_isPaused = !g_isPaused;
                SetWindowText(g_hwndPauseButton, g_isPaused ? L"Resume" : L"Pause");
            }
            break;

        case 3: // Stop button
            g_isRunning = false;
            g_isPaused = false;
            EnableWindow(g_hwndStartButton, TRUE);
            EnableWindow(g_hwndPauseButton, FALSE);
            EnableWindow(g_hwndStopButton, FALSE);
            SetWindowText(g_hwndPauseButton, L"Pause");
            break;

        case 999: // Update UI message
            UpdateListView();
            break;

        case 1000: // Scheduler completed
            MessageBox(hwnd, L"All processes completed!", L"Scheduler Complete", MB_OK | MB_ICONINFORMATION);
            EnableWindow(g_hwndStartButton, TRUE);
            EnableWindow(g_hwndPauseButton, FALSE);
            EnableWindow(g_hwndStopButton, FALSE);
            ShowGanttChart(hwnd);  // Add this line
            break;

        case 4: // Add Process button
        {
            wchar_t processName[256];
            wchar_t burstTimeStr[32];
            wchar_t appearingTimeStr[32];
            GetWindowText(g_hwndProcessNameEdit, processName, 256);
            GetWindowText(g_hwndBurstTimeEdit, burstTimeStr, 32);
            GetWindowText(g_hwndAppearingTimeEdit, appearingTimeStr, 32);

            // Validate input
            if (wcslen(processName) == 0) {
                MessageBox(hwnd, L"Please enter a process name.", L"Input Error", MB_OK | MB_ICONWARNING);
                return 0;
            }

            int burstTime = _wtoi(burstTimeStr);
            if (burstTime <= 0) {
                MessageBox(hwnd, L"Please enter a valid burst time (positive integer).", L"Input Error", MB_OK | MB_ICONWARNING);
                return 0;
            }

            int appearingTime = _wtoi(appearingTimeStr);
            if (appearingTime < 0) {
                MessageBox(hwnd, L"Please enter a valid appearing time (non-negative integer).", L"Input Error", MB_OK | MB_ICONWARNING);
                return 0;
            }

            // Add new process
            {
                std::lock_guard<std::mutex> lock(g_processMutex);
                Process newProcess = {
                    processName,           // name
                    burstTime,            // burstTime
                    burstTime,            // remainingTime
                    appearingTime,        // appearingTime
                    0,                    // waitingTime
                    0,                    // turnaroundTime
                    false                 // completed
                };
                g_processes.push_back(newProcess);
            }

            // Clear input fields
            SetWindowText(g_hwndProcessNameEdit, L"");
            SetWindowText(g_hwndBurstTimeEdit, L"");
            SetWindowText(g_hwndAppearingTimeEdit, L"");

            // Update ListView
            UpdateListView();
            break;
        }
        }
        return 0;

    case WM_CHAR:
        if (GetDlgCtrlID((HWND)lParam) == GetDlgCtrlID(g_hwndBurstTimeEdit) ||
            GetDlgCtrlID((HWND)lParam) == GetDlgCtrlID(g_hwndAppearingTimeEdit)) {
            // Only allow digits and control characters (backspace, delete, etc.)
            if (!isdigit(wParam) && wParam != VK_BACK && wParam != VK_DELETE) {
                return 0;  // Ignore non-digit characters
            }
        }
        break;

    case WM_DESTROY:
        g_isRunning = false;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // Initialize common controls
    INITCOMMONCONTROLSEX icex = { sizeof(INITCOMMONCONTROLSEX), ICC_LISTVIEW_CLASSES };
    InitCommonControlsEx(&icex);

    // Register window class
    const wchar_t CLASS_NAME[] = L"SRTNSchedulerWindow";

    WNDCLASS wc = { 0 };
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);

    RegisterClass(&wc);

    // Create main window
    HWND hwnd = CreateWindowEx(
        0,
        CLASS_NAME,
        L"Shortest Remaining Time Next Scheduler (64-bit)",
        WS_OVERLAPPEDWINDOW & (~WS_MAXIMIZEBOX) & (~WS_THICKFRAME),
        CW_USEDEFAULT, CW_USEDEFAULT, 850, 398,  // Increased width to 850 for better spacing
        NULL, NULL, hInstance, NULL);

    if (hwnd == NULL) {
        MessageBox(NULL, L"Window Creation Failed!", L"Error", MB_OK | MB_ICONERROR);
        return 0;
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    // Message loop
    MSG msg = { 0 };
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return (int)msg.wParam;
}