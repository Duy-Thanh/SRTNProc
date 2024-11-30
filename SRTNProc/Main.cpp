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

// Update these global constants
const int HEADER_HEIGHT = 100;      // Taller header
const int TIMELINE_HEIGHT = 60;     // Taller timeline
const int PROCESS_PADDING = 12;     // Consistent padding
#define COLOR_BACKGROUND RGB(250, 250, 250)      // Off-white background
#define COLOR_TEXT RGB(60, 64, 67)              // Google Gray 900
#define COLOR_GRID RGB(241, 243, 244)           // Google Gray 100
#define COLOR_TIMELINE RGB(255, 255, 255)       // Pure white
#define COLOR_SUBTITLE RGB(95, 99, 104)         // Google Gray 700
#define COLOR_PROCESS_BG RGB(232, 240, 254)     // Light blue background

// Add this helper function before GanttWindowProc
void FillRoundRect(HDC hdc, const RECT* rect, int radius, HBRUSH brush) {
    SelectObject(hdc, brush);
    BeginPath(hdc);
    RoundRect(hdc, rect->left, rect->top, rect->right, rect->bottom, radius * 2, radius * 2);
    EndPath(hdc);
    FillPath(hdc);
}

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
        { L"Process Name", 100 },
        { L"Time using CPU", 100 },
        { L"Appearing Time", 100 },
        { L"Waiting Time", 100 },
        { L"Turnaround Time", 150 },
        { L"Process Status", 100 }
    };

    for (int i = 0; i < _countof(columns); i++) {
        lvc.pszText = const_cast<LPWSTR>(columns[i].name);
        lvc.cx = columns[i].width;
        ListView_InsertColumn(hwndListView, i, &lvc);
    }

    // Set extended ListView styles
    ListView_SetExtendedListViewStyle(hwndListView,
        LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
}

// Window Procedure
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE: {
        // Create buttons
        g_hwndStartButton = CreateWindow(
            L"BUTTON", L"Start",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            10, 10, 100, 30,
            hwnd, (HMENU)1, NULL, NULL);

        g_hwndPauseButton = CreateWindow(
            L"BUTTON", L"Pause",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            120, 10, 100, 30,
            hwnd, (HMENU)2, NULL, NULL);

        g_hwndStopButton = CreateWindow(
            L"BUTTON", L"Stop",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            230, 10, 100, 30,
            hwnd, (HMENU)3, NULL, NULL);

        // Create ListView
        g_hwndListView = CreateWindow(
            WC_LISTVIEW, L"",
            WS_CHILD | WS_VISIBLE | LVS_REPORT | WS_BORDER,
            10, 50, 700, 300,
            hwnd, (HMENU)4, NULL, NULL);

        InitializeListView(g_hwndListView);

        EnableWindow(g_hwndPauseButton, FALSE);
        EnableWindow(g_hwndStopButton, FALSE);
        return 0;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case 1: // Start button
            if (!g_isRunning) {
                // Initialize processes
                {
                    g_executionSequence.clear();  // Add this line
                    std::lock_guard<std::mutex> lock(g_processMutex);
                    //struct Process {
                    //    std::wstring name;              // Process name
                    //    int burstTime;                  // Burst time
                    //    int remainingTime;              // Remaining time
                    //    int waitingTime;                // Waiting time
                    //    int turnaroundTime;             // Turn-around time
                    //    bool completed;
                    //};
                    g_processes = {
                        { L"Process 1", 2, 8, 0, 0, false },
                        { L"Process 2", 0, 4, 0, 0, false },
                        { L"Process 3", 1, 6, 0, 0, false }
                    };
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
        }
        return 0;

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
        CW_USEDEFAULT, CW_USEDEFAULT, 735, 398,
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