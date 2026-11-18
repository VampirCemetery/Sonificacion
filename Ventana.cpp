// Ventana.cpp - Interfaz de usuario con MFC (Microsoft Foundation Classes).
// Aquí NO se incluye OpenCV: el análisis de la imagen y el audio están en Motor.cpp.
// Todo el programa usa clases MFC: CWinApp, CFrameWnd, CWnd, CButton, CStatic,
// CSliderCtrl, CFileDialog. No hay WinMain, WndProc ni CreateWindow.
#include <afxwin.h>      // MFC: aplicación, ventana principal y controles
#include <afxdlgs.h>     // MFC: cuadros de abrir y guardar (CFileDialog)
#include <afxcmn.h>      // MFC: controles comunes (CSliderCtrl)
#include <mmsystem.h>    // PlaySound
#include <algorithm>
#include <string>
#include <vector>
#include <fstream>
#include <cstdint>
#include <cstring>
#include "Motor.h"

#pragma comment(lib, "winmm.lib")

// ---------------------------------------------------------------
// Identificadores de controles
// ---------------------------------------------------------------
#define ID_BTN_ABRIR      201
#define ID_BTN_PIXELES    211
#define ID_BTN_LINEAS     212
#define ID_BTN_PUNTOS     213
#define ID_BTN_COLORES    214
#define ID_BTN_REPRODUCIR 221
#define ID_BTN_GUARDAR    222
#define ID_VISOR_ORIG     230
#define ID_SLD_ZOOM       231
#define ID_SLD_VOL        232
#define ID_SLD_TONO       233
#define ID_SLD_PROG       234
#define ID_VISOR_PROC     235
#define ID_TIMER_BARRIDO  1

// ===============================================================
// CVisor: ventana propia (hija de la principal) que muestra una imagen,
// con zoom y barras de desplazamiento (scrollbars) si se desea.
// ===============================================================
class CVisor : public CWnd {
public:
    BOOL Crear(CWnd* padre, UINT id, bool conScroll) {
        m_scroll = conScroll;
        DWORD estilo = WS_CHILD | WS_VISIBLE | WS_BORDER;
        if (conScroll) estilo |= WS_HSCROLL | WS_VSCROLL;
        return Create(AfxRegisterWndClass(CS_HREDRAW | CS_VREDRAW, ::LoadCursor(nullptr, IDC_ARROW)),
            L"", estilo, CRect(0, 0, 0, 0), padre, id);
    }

    // Pone una imagen nueva (zoom = ajustado a la ventana)
    void SetImagen(const ImagenBGR& im) {
        if (im.vacia()) { Limpiar(); return; }
        m_ancho = im.ancho; m_alto = im.alto; m_factor = 1.0;
        size_t n = (size_t)m_ancho * (size_t)m_alto;
        m_px.resize(n * 4);
        for (size_t i = 0; i < n; i++) {          // BGR (3 bytes) -> BGRA (4 bytes)
            m_px[4 * i + 0] = im.datos[3 * i + 0];
            m_px[4 * i + 1] = im.datos[3 * i + 1];
            m_px[4 * i + 2] = im.datos[3 * i + 2];
            m_px[4 * i + 3] = 255;
        }
        if (m_scroll) { SetScrollPos(SB_HORZ, 0, FALSE); SetScrollPos(SB_VERT, 0, FALSE); }
        ActualizarScroll();
        Invalidate(FALSE);
    }

    void Limpiar() {
        m_px.clear(); m_ancho = m_alto = 0;
        if (m_scroll) ShowScrollBar(SB_BOTH, FALSE);
        Invalidate(FALSE);
    }

    // f = 1.0 (ajustado a la ventana) hasta 8.0. Mantiene el centro de lo que se ve.
    void SetFactor(double f) {
        if (m_px.empty()) { m_factor = f; return; }
        CRect rc; GetClientRect(&rc);
        double z0 = Zoom();
        double cx = (PosH() + rc.Width() / 2.0) / z0;
        double cy = (PosV() + rc.Height() / 2.0) / z0;
        m_factor = f;
        ActualizarScroll();
        if (m_scroll) {
            double z1 = Zoom();
            GetClientRect(&rc);
            SetScrollPos(SB_HORZ, (int)(cx * z1 - rc.Width() / 2.0), TRUE);
            SetScrollPos(SB_VERT, (int)(cy * z1 - rc.Height() / 2.0), TRUE);
        }
        Invalidate(FALSE);
    }

    // Zona de la imagen que se ve ahora (en píxeles de la imagen)
    bool RegionVisible(int& x, int& y, int& w, int& h) {
        if (m_px.empty()) return false;
        double z = Zoom();
        CRect rc; GetClientRect(&rc);
        x = (int)(PosH() / z);
        y = (int)(PosV() / z);
        w = (int)((std::min)((double)rc.Width(), m_ancho * z) / z + 0.5);
        h = (int)((std::min)((double)rc.Height(), m_alto * z) / z + 0.5);
        x = (std::max)(0, (std::min)(x, m_ancho - 1));
        y = (std::max)(0, (std::min)(y, m_alto - 1));
        w = (std::min)(w, m_ancho - x);
        h = (std::min)(h, m_alto - y);
        return true;
    }

protected:
    std::vector<uint8_t> m_px;     // imagen en BGRA
    int m_ancho = 0, m_alto = 0;
    double m_factor = 1.0;
    bool m_scroll = true;

    int PosH() { return m_scroll ? GetScrollPos(SB_HORZ) : 0; }
    int PosV() { return m_scroll ? GetScrollPos(SB_VERT) : 0; }

    // Zoom que hace que la imagen quepa entera en la ventana (no depende de las scrollbars)
    double Ajuste() {
        CRect w; GetWindowRect(&w);
        double vw = w.Width() - 2.0, vh = w.Height() - 2.0;   // menos el borde
        if (m_ancho <= 0 || m_alto <= 0 || vw < 1 || vh < 1) return 1.0;
        return (std::min)(vw / m_ancho, vh / m_alto);
    }
    double Zoom() { return Ajuste() * m_factor; }

    void ActualizarScroll() {
        if (!m_scroll || m_px.empty()) return;
        for (int pasada = 0; pasada < 2; pasada++) {   // 2 pasadas: al aparecer una barra cambia el área
            CRect rc; GetClientRect(&rc);
            double z = Zoom();
            SCROLLINFO si = {};
            si.cbSize = sizeof(si);
            si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
            si.nMin = 0;

            si.nMax = (int)(m_ancho * z) - 1;
            si.nPage = (UINT)rc.Width();
            si.nPos = (std::max)(0, (std::min)(GetScrollPos(SB_HORZ), si.nMax - (int)si.nPage + 1));
            SetScrollInfo(SB_HORZ, &si, TRUE);

            si.nMax = (int)(m_alto * z) - 1;
            si.nPage = (UINT)rc.Height();
            si.nPos = (std::max)(0, (std::min)(GetScrollPos(SB_VERT), si.nMax - (int)si.nPage + 1));
            SetScrollInfo(SB_VERT, &si, TRUE);
        }
    }

    void Mover(int barra, UINT code) {
        if (!m_scroll) return;
        SCROLLINFO si = {};
        si.cbSize = sizeof(si);
        si.fMask = SIF_ALL;
        GetScrollInfo(barra, &si);
        int p = si.nPos;
        switch (code) {
        case SB_LINEUP:   p -= 30; break;
        case SB_LINEDOWN: p += 30; break;
        case SB_PAGEUP:   p -= (int)si.nPage; break;
        case SB_PAGEDOWN: p += (int)si.nPage; break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: p = si.nTrackPos; break;
        default: return;
        }
        SetScrollPos(barra, p, TRUE);
        Invalidate(FALSE);
    }

    afx_msg void OnPaint() {
        CPaintDC dc(this);
        CRect rc; GetClientRect(&rc);
        dc.FillSolidRect(rc, RGB(30, 30, 30));
        if (m_px.empty()) return;

        double z = Zoom();
        int dw = (int)(std::min)((double)rc.Width(), m_ancho * z);
        int dh = (int)(std::min)((double)rc.Height(), m_alto * z);
        int ox = (rc.Width() - dw) / 2;       // si cabe entera, se centra
        int oy = (rc.Height() - dh) / 2;
        int sx = (int)(PosH() / z), sy = (int)(PosV() / z);
        int sw = (std::min)(m_ancho - sx, (int)(dw / z + 0.5));
        int sh = (std::min)(m_alto - sy, (int)(dh / z + 0.5));
        if (dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0) return;

        BITMAPINFO bmi = {};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = m_ancho;
        bmi.bmiHeader.biHeight = -m_alto;          // negativo = de arriba hacia abajo
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        dc.SetStretchBltMode(z < 1.0 ? HALFTONE : COLORONCOLOR);
        int syDib = m_alto - sy - sh;   // Windows cuenta la fila desde abajo
        ::StretchDIBits(dc.GetSafeHdc(), ox, oy, dw, dh, sx, syDib, sw, sh,
            m_px.data(), &bmi, DIB_RGB_COLORS, SRCCOPY);
    }

    afx_msg void OnSize(UINT t, int cx, int cy) {
        CWnd::OnSize(t, cx, cy);
        ActualizarScroll();
        Invalidate(FALSE);
    }
    afx_msg void OnHScroll(UINT c, UINT p, CScrollBar*) { Mover(SB_HORZ, c); }
    afx_msg void OnVScroll(UINT c, UINT p, CScrollBar*) { Mover(SB_VERT, c); }
    afx_msg BOOL OnMouseWheel(UINT, short d, CPoint) {
        Mover(SB_VERT, d > 0 ? SB_LINEUP : SB_LINEDOWN);
        return TRUE;
    }
    afx_msg void OnLButtonDown(UINT, CPoint) { SetFocus(); }   // para que la rueda funcione
    afx_msg BOOL OnEraseBkgnd(CDC*) { return TRUE; }

    DECLARE_MESSAGE_MAP()
};

BEGIN_MESSAGE_MAP(CVisor, CWnd)
    ON_WM_PAINT()
    ON_WM_SIZE()
    ON_WM_HSCROLL()
    ON_WM_VSCROLL()
    ON_WM_MOUSEWHEEL()
    ON_WM_LBUTTONDOWN()
    ON_WM_ERASEBKGND()
END_MESSAGE_MAP()

// ===============================================================
// Ventana principal
// ===============================================================
class CVentanaPrincipal : public CFrameWnd {
public:
    CVentanaPrincipal();

protected:
    // --- controles ---
    CFont   m_fuente;
    CButton m_btnAbrir, m_btnPixeles, m_btnLineas, m_btnPuntos, m_btnColores;
    CButton m_btnReproducir, m_btnGuardar;
    CStatic m_lblOriginal, m_lblProceso, m_lblEstado, m_lblTiempo;
    CStatic m_lblZoom, m_lblVol, m_lblTono;
    CVisor  m_visorOrig, m_visorProc;
    CSliderCtrl m_sldZoom, m_sldVol, m_sldTono, m_sldProg;

    // --- estado ---
    ImagenBGR m_orig;                 // la zona que se está analizando
    int    m_metodo = 0;              // 0 = ninguno, 1 = píxeles, 2 = líneas, 3 = puntos, 4 = colores
    double m_barridoX = 0;            // columna actual de la línea roja (método de píxeles)
    ULONGLONG m_inicio = 0;           // momento (ms) en que arrancó la reproducción
    std::vector<uint8_t> m_wav;       // WAV completo en memoria
    std::vector<uint8_t> m_play;      // WAV que se está reproduciendo (debe seguir vivo mientras suena)
    double m_offset = 0;              // segundo desde el que se empezó a reproducir
    double m_duracion = 0;            // duración total (s)
    int    m_volumen = 80;            // 0..100
    bool   m_sonando = false;
    bool   m_arrastrando = false;     // el usuario está moviendo la barra de progreso

    // --- funciones ---
    void AcomodarElementos(int ancho, int alto);
    void RefrescarProceso();
    void GenerarAudio(int metodo);
    void ReproducirDesde(double seg);
    void GuardarAudio();
    void SeleccionarMetodo(int metodo);
    void AbrirImagen();
    double TiempoActual() const { return m_offset + (double)(GetTickCount64() - m_inicio) / 1000.0; }
    void ActualizarTiempo(double t);

    // --- mensajes ---
    afx_msg int  OnCreate(LPCREATESTRUCT lpCreateStruct);
    afx_msg void OnSize(UINT nType, int cx, int cy);
    afx_msg void OnTimer(UINT_PTR nIDEvent);
    afx_msg void OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar);
    afx_msg void OnDestroy();

    afx_msg void OnAbrir() { AbrirImagen(); }
    afx_msg void OnPixeles() { SeleccionarMetodo(1); }
    afx_msg void OnLineas() { SeleccionarMetodo(2); }
    afx_msg void OnPuntos() { SeleccionarMetodo(3); }
    afx_msg void OnColores() { SeleccionarMetodo(4); }
    afx_msg void OnReproducir() { ReproducirDesde(0); }
    afx_msg void OnGuardar() { GuardarAudio(); }

    DECLARE_MESSAGE_MAP()
};

BEGIN_MESSAGE_MAP(CVentanaPrincipal, CFrameWnd)
    ON_WM_CREATE()
    ON_WM_SIZE()
    ON_WM_TIMER()
    ON_WM_HSCROLL()
    ON_WM_DESTROY()
    ON_BN_CLICKED(ID_BTN_ABRIR, OnAbrir)
    ON_BN_CLICKED(ID_BTN_PIXELES, OnPixeles)
    ON_BN_CLICKED(ID_BTN_LINEAS, OnLineas)
    ON_BN_CLICKED(ID_BTN_PUNTOS, OnPuntos)
    ON_BN_CLICKED(ID_BTN_COLORES, OnColores)
    ON_BN_CLICKED(ID_BTN_REPRODUCIR, OnReproducir)
    ON_BN_CLICKED(ID_BTN_GUARDAR, OnGuardar)
END_MESSAGE_MAP()

CVentanaPrincipal::CVentanaPrincipal() {
    Create(nullptr, L"Sonificación visual", WS_OVERLAPPEDWINDOW, CRect(0, 0, 1200, 780));
}

int CVentanaPrincipal::OnCreate(LPCREATESTRUCT lpCreateStruct) {
    if (CFrameWnd::OnCreate(lpCreateStruct) == -1) return -1;

    m_fuente.CreatePointFont(90, L"Segoe UI");

    const DWORD BTN = WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON;
    const DWORD LBL = WS_CHILD | WS_VISIBLE;
    const DWORD SLD = WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS;
    CRect r(0, 0, 0, 0);

    m_btnAbrir.Create(L"Abrir imagen", BTN, r, this, ID_BTN_ABRIR);
    m_btnPixeles.Create(L"Píxeles", BTN, r, this, ID_BTN_PIXELES);
    m_btnLineas.Create(L"Líneas", BTN, r, this, ID_BTN_LINEAS);
    m_btnPuntos.Create(L"Puntos", BTN, r, this, ID_BTN_PUNTOS);
    m_btnColores.Create(L"Colores", BTN, r, this, ID_BTN_COLORES);
    m_btnReproducir.Create(L"Reproducir", BTN, r, this, ID_BTN_REPRODUCIR);
    m_btnGuardar.Create(L"Guardar audio", BTN, r, this, ID_BTN_GUARDAR);

    m_lblOriginal.Create(L"Original (zoom: se analiza lo que ves)", LBL, r, this);
    m_lblProceso.Create(L"Procesamiento", LBL, r, this);
    m_lblEstado.Create(L"Abre una imagen para empezar.", LBL, r, this);
    m_lblTiempo.Create(L"0:00 / 0:00", LBL, r, this);
    m_lblZoom.Create(L"Zoom", LBL, r, this);
    m_lblVol.Create(L"Volumen", LBL, r, this);
    m_lblTono.Create(L"Tono", LBL, r, this);

    m_visorOrig.Crear(this, ID_VISOR_ORIG, true);    // con scrollbars
    m_visorProc.Crear(this, ID_VISOR_PROC, false);   // sin scrollbars

    m_sldZoom.Create(SLD, r, this, ID_SLD_ZOOM); m_sldZoom.SetRange(100, 800); m_sldZoom.SetPos(100);
    m_sldVol.Create(SLD, r, this, ID_SLD_VOL);   m_sldVol.SetRange(0, 100);    m_sldVol.SetPos(m_volumen);
    m_sldTono.Create(SLD, r, this, ID_SLD_TONO); m_sldTono.SetRange(-12, 12);  m_sldTono.SetPos(0);
    m_sldProg.Create(SLD, r, this, ID_SLD_PROG); m_sldProg.SetRange(0, 1000);  m_sldProg.SetPos(0);

    CWnd* conFuente[] = { &m_btnAbrir, &m_btnPixeles, &m_btnLineas, &m_btnPuntos, &m_btnColores,
                          &m_btnReproducir, &m_btnGuardar, &m_lblOriginal, &m_lblProceso,
                          &m_lblEstado, &m_lblTiempo, &m_lblZoom, &m_lblVol, &m_lblTono };
    for (CWnd* c : conFuente) c->SetFont(&m_fuente);

    // Los métodos se activan al abrir una imagen; el audio, al elegir un método
    m_btnPixeles.EnableWindow(FALSE);
    m_btnLineas.EnableWindow(FALSE);
    m_btnPuntos.EnableWindow(FALSE);
    m_btnColores.EnableWindow(FALSE);
    m_btnReproducir.EnableWindow(FALSE);
    m_btnGuardar.EnableWindow(FALSE);
    m_sldProg.EnableWindow(FALSE);
    m_sldZoom.EnableWindow(FALSE);
    return 0;
}

void CVentanaPrincipal::OnSize(UINT nType, int cx, int cy) {
    CFrameWnd::OnSize(nType, cx, cy);
    if (m_btnAbrir.m_hWnd) AcomodarElementos(cx, cy);
}

// ---------------------------------------------------------------
// Distribución de controles
// ---------------------------------------------------------------
void CVentanaPrincipal::AcomodarElementos(int ancho, int alto) {
    if (ancho <= 0 || alto <= 0) return;

    const int margen = 20;
    const int altoBoton = 32;

    m_btnAbrir.MoveWindow(margen, 15, 140, altoBoton);
    m_btnPixeles.MoveWindow(margen + 160, 15, 110, altoBoton);
    m_btnLineas.MoveWindow(margen + 280, 15, 110, altoBoton);
    m_btnPuntos.MoveWindow(margen + 400, 15, 110, altoBoton);
    m_btnColores.MoveWindow(margen + 520, 15, 110, altoBoton);

    int yEtiqueta = 58;
    int yLienzo = 78;
    int altoLienzo = alto - yLienzo - 140;
    if (altoLienzo < 100) altoLienzo = 100;
    int anchoLienzo = (ancho - margen * 3) / 2;
    int xIzq = margen;
    int xDer = margen * 2 + anchoLienzo;

    m_lblOriginal.MoveWindow(xIzq, yEtiqueta, 320, 18);
    m_lblProceso.MoveWindow(xDer, yEtiqueta, 200, 18);
    m_visorOrig.MoveWindow(xIzq, yLienzo, anchoLienzo, altoLienzo);
    m_visorProc.MoveWindow(xDer, yLienzo, anchoLienzo, altoLienzo);

    int yAbajo = yLienzo + altoLienzo + 10;
    m_lblEstado.MoveWindow(margen, yAbajo, ancho - margen * 2, 20);

    // Fila 2: reproducir, guardar y barra de progreso
    m_btnReproducir.MoveWindow(margen, yAbajo + 28, 140, altoBoton);
    m_btnGuardar.MoveWindow(margen + 150, yAbajo + 28, 160, altoBoton);
    int xProg = margen + 330;
    int wProg = ancho - margen - xProg - 95;
    if (wProg < 50) wProg = 50;
    m_sldProg.MoveWindow(xProg, yAbajo + 28, wProg, altoBoton);
    m_lblTiempo.MoveWindow(ancho - margen - 85, yAbajo + 36, 85, 20);

    // Fila 3: zoom, volumen, tono
    int yS = yAbajo + 72;
    m_lblZoom.MoveWindow(margen, yS + 3, 50, 20);
    m_sldZoom.MoveWindow(margen + 55, yS, 200, 24);
    m_lblVol.MoveWindow(margen + 285, yS + 3, 60, 20);
    m_sldVol.MoveWindow(margen + 350, yS, 200, 24);
    m_lblTono.MoveWindow(margen + 580, yS + 3, 45, 20);
    m_sldTono.MoveWindow(margen + 630, yS, 200, 24);
}

// ---------------------------------------------------------------
// Vista de procesamiento (lienzo derecho)
// ---------------------------------------------------------------
void CVentanaPrincipal::RefrescarProceso() {
    if (m_orig.vacia() || m_metodo == 0) return;

    ImagenBGR vista;
    switch (m_metodo) {
    case 1: vista = Motor::VistaPixeles((int)m_barridoX); break;
    case 2: vista = Motor::VistaLineas();                 break;
    case 3: vista = Motor::VistaPuntos();                 break;
    case 4: vista = Motor::VistaColores();                break;
    }
    m_visorProc.SetImagen(vista);
}

// ---------------------------------------------------------------
// Audio: generar, reproducir (con seek y volumen) y guardar
// ---------------------------------------------------------------
void CVentanaPrincipal::GenerarAudio(int metodo) {
    PlaySoundA(nullptr, nullptr, 0);   // detener lo que esté sonando antes de cambiar el audio
    KillTimer(ID_TIMER_BARRIDO);
    m_wav = Motor::GenerarWav(metodo);

    m_duracion = (m_wav.size() > 44) ? (double)(m_wav.size() - 44) / 2.0 / SR_AUDIO : 0.0;
    m_sonando = false;
    m_offset = 0;
    m_sldProg.SetPos(0);
    ActualizarTiempo(0);

    m_btnReproducir.EnableWindow(TRUE);
    m_btnGuardar.EnableWindow(TRUE);
    m_sldProg.EnableWindow(TRUE);
}

// Arma un WAV nuevo que empieza en el segundo pedido y con el volumen aplicado.
// PlaySound no puede saltar a un segundo ni cambiar el volumen, por eso se hace así.
void CVentanaPrincipal::ReproducirDesde(double seg) {
    if (m_wav.size() <= 44) return;

    size_t total = (m_wav.size() - 44) / 2;                    // muestras
    if (seg < 0) seg = 0;
    size_t n0 = (std::min)((size_t)(seg * SR_AUDIO), total - 1);
    uint32_t datos = (uint32_t)((total - n0) * 2);
    uint32_t riff = 36 + datos;

    PlaySoundA(nullptr, nullptr, 0);                           // parar antes de tocar m_play

    m_play.assign(44 + datos, 0);
    memcpy(m_play.data(), m_wav.data(), 44);                   // cabecera
    memcpy(&m_play[4], &riff, 4);
    memcpy(&m_play[40], &datos, 4);
    const int16_t* src = (const int16_t*)(m_wav.data() + 44) + n0;
    int16_t* dst = (int16_t*)(m_play.data() + 44);
    double ganancia = m_volumen / 100.0;
    for (size_t i = 0; i < total - n0; i++) dst[i] = (int16_t)(src[i] * ganancia);

    m_offset = (double)n0 / SR_AUDIO;
    m_inicio = GetTickCount64();
    m_sonando = true;
    SetTimer(ID_TIMER_BARRIDO, 30, nullptr);
    PlaySoundA((LPCSTR)m_play.data(), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}

void CVentanaPrincipal::GuardarAudio() {
    if (m_wav.empty()) return;

    CFileDialog dlg(FALSE, L"wav", L"sonificacion.wav",
        OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY,
        L"Audio WAV (*.wav)|*.wav||", this);
    if (dlg.DoModal() != IDOK) return;

    CString ruta = dlg.GetPathName();
    std::ofstream f(ruta.GetString(), std::ios::binary);
    if (!f) {
        AfxMessageBox(L"No se pudo guardar el archivo.", MB_OK | MB_ICONERROR);
        return;
    }
    f.write((const char*)m_wav.data(), (std::streamsize)m_wav.size());
    m_lblEstado.SetWindowText(L"Audio guardado.");
}

void CVentanaPrincipal::ActualizarTiempo(double t) {
    int a = (int)t, b = (int)m_duracion;
    CString s;
    s.Format(L"%d:%02d / %d:%02d", a / 60, a % 60, b / 60, b % 60);
    m_lblTiempo.SetWindowText(s);
}

// ---------------------------------------------------------------
// Selección de método y apertura de imagen
// ---------------------------------------------------------------
void CVentanaPrincipal::SeleccionarMetodo(int metodo) {
    if (m_orig.vacia()) return;

    KillTimer(ID_TIMER_BARRIDO);
    m_sonando = false;

    // Se analiza SOLO la zona que se ve en el visor (con el zoom y las scrollbars actuales)
    int x, y, w, h;
    if (!m_visorOrig.RegionVisible(x, y, w, h) || !Motor::EstablecerRegion(x, y, w, h)) {
        m_lblEstado.SetWindowText(L"La zona visible es muy pequeña. Baja el zoom e inténtalo de nuevo.");
        return;
    }
    m_orig = Motor::ImagenOriginal();   // ahora m_orig = SOLO la zona visible

    m_metodo = metodo;
    m_barridoX = 0;

    const wchar_t* texto = L"";
    switch (metodo) {
    case 1: texto = L"Píxeles: la imagen son 24 notas (franjas); el brillo de cada franja es su volumen. Pulsa Reproducir."; break;
    case 2: texto = L"Líneas: orientación = nota, longitud = duración y volumen, posición = momento. Pulsa Reproducir."; break;
    case 3: texto = L"Puntos: x = momento, y = nota (arriba agudo, abajo grave), brillo = volumen. Pulsa Reproducir."; break;
    case 4: texto = L"Colores: tono = nota, brillo = volumen, saturación = timbre, porcentaje = duración. Pulsa Reproducir."; break;
    }
    m_lblEstado.SetWindowText(texto);

    GenerarAudio(metodo);
    RefrescarProceso();
}

void CVentanaPrincipal::AbrirImagen() {
    CFileDialog dlg(TRUE, nullptr, nullptr,
        OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY,
        L"Imágenes|*.png;*.jpg;*.jpeg;*.bmp|Todos los archivos|*.*||", this);
    if (dlg.DoModal() != IDOK) return;

    CString ruta = dlg.GetPathName();
    if (!Motor::CargarImagen(std::wstring(ruta.GetString()))) {
        AfxMessageBox(L"No se pudo cargar la imagen.", MB_OK | MB_ICONERROR);
        return;
    }

    m_orig = Motor::ImagenOriginal();
    m_metodo = 0;
    KillTimer(ID_TIMER_BARRIDO);
    PlaySoundA(nullptr, nullptr, 0);
    m_wav.clear();
    m_sonando = false;
    m_duracion = 0;
    m_offset = 0;
    m_sldProg.SetPos(0);
    ActualizarTiempo(0);

    m_sldZoom.SetPos(100);
    m_sldZoom.EnableWindow(TRUE);
    m_visorOrig.SetImagen(Motor::ImagenCompleta());
    m_visorProc.Limpiar();
    m_lblEstado.SetWindowText(L"Imagen cargada. Haz zoom a una zona si quieres y elige un método.");

    m_btnPixeles.EnableWindow(TRUE);
    m_btnLineas.EnableWindow(TRUE);
    m_btnPuntos.EnableWindow(TRUE);
    m_btnColores.EnableWindow(TRUE);
    m_btnReproducir.EnableWindow(FALSE);
    m_btnGuardar.EnableWindow(FALSE);
    m_sldProg.EnableWindow(FALSE);
}

// ---------------------------------------------------------------
// Timer: mueve la barra de progreso y la línea roja (método de píxeles)
// ---------------------------------------------------------------
void CVentanaPrincipal::OnTimer(UINT_PTR nIDEvent) {
    if (nIDEvent == ID_TIMER_BARRIDO && m_sonando) {
        // La posición sale del tiempo real transcurrido, así se mantiene sincronizada con el audio
        double t = TiempoActual();
        if (t >= m_duracion) {
            t = m_duracion;
            m_sonando = false;
            KillTimer(ID_TIMER_BARRIDO);
        }
        if (!m_arrastrando && m_duracion > 0) {
            m_sldProg.SetPos((int)(t / m_duracion * 1000));
            ActualizarTiempo(t);
            if (m_metodo == 1 && !m_orig.vacia()) {
                m_barridoX = t / m_duracion * (m_orig.ancho - 1);
                RefrescarProceso();
            }
        }
    }
    CFrameWnd::OnTimer(nIDEvent);
}

// ---------------------------------------------------------------
// Sliders (los CSliderCtrl avisan a la ventana con WM_HSCROLL)
// ---------------------------------------------------------------
void CVentanaPrincipal::OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar) {
    HWND h = pScrollBar ? pScrollBar->m_hWnd : nullptr;

    if (h && h == m_sldZoom.m_hWnd) {
        m_visorOrig.SetFactor(m_sldZoom.GetPos() / 100.0);
    }
    else if (h && h == m_sldVol.m_hWnd) {
        m_volumen = m_sldVol.GetPos();
        if (nSBCode == TB_ENDTRACK && m_sonando)
            ReproducirDesde(TiempoActual());        // aplica el nuevo volumen sin perder la posición
    }
    else if (h && h == m_sldTono.m_hWnd) {
        if (nSBCode == TB_ENDTRACK) {
            Motor::SetTono(m_sldTono.GetPos());
            if (m_metodo != 0) SeleccionarMetodo(m_metodo);   // regenera el audio con el nuevo tono
        }
    }
    else if (h && h == m_sldProg.m_hWnd) {
        if (nSBCode == TB_ENDTRACK) {
            m_arrastrando = false;
            if (m_duracion > 0) ReproducirDesde(m_sldProg.GetPos() / 1000.0 * m_duracion);
        }
        else {
            m_arrastrando = true;
            if (m_duracion > 0) {
                double t = m_sldProg.GetPos() / 1000.0 * m_duracion;
                ActualizarTiempo(t);
                if (m_metodo == 1 && !m_orig.vacia()) {   // la línea roja sigue al slider
                    m_barridoX = t / m_duracion * (m_orig.ancho - 1);
                    RefrescarProceso();
                }
            }
        }
    }
    CFrameWnd::OnHScroll(nSBCode, nPos, pScrollBar);
}

void CVentanaPrincipal::OnDestroy() {
    KillTimer(ID_TIMER_BARRIDO);
    PlaySoundA(nullptr, nullptr, 0);
    CFrameWnd::OnDestroy();
}

// ===============================================================
// MFC: aplicación
// ===============================================================
class CAplicacionSonificacion : public CWinApp {
public:
    BOOL InitInstance() override {
        CWinApp::InitInstance();
        CVentanaPrincipal* ventana = new CVentanaPrincipal;   // MFC la libera al cerrarse
        m_pMainWnd = ventana;
        ventana->CenterWindow();
        ventana->ShowWindow(m_nCmdShow);
        ventana->UpdateWindow();
        return TRUE;
    }
};

CAplicacionSonificacion theApp;   // el objeto global que arranca la aplicación MFC
// MFC trae esta función, pero este encabezado no la declara; la declaramos nosotros
int AFXAPI AfxWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPTSTR lpCmdLine, int nCmdShow);

// Punto de entrada: le pasa el control a MFC
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR, int nShow) {
    return AfxWinMain(hInst, hPrev, GetCommandLineW(), nShow);
}