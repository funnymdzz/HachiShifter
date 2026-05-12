import sys
import os
import numpy as np
import scipy.signal as signal
import librosa
import sounddevice as sd
import soundfile as sf  # 新增：用于导出音频
from PyQt5.QtWidgets import (QApplication, QMainWindow, QWidget, QVBoxLayout, 
                             QHBoxLayout, QPushButton, QSlider, QLabel, QFileDialog, QMessageBox, QShortcut)
from PyQt5.QtCore import Qt, pyqtSignal, QPointF
from PyQt5.QtGui import QPainter, QPen, QColor, QBrush, QFont, QKeySequence

# ==========================================
# 1. 音频处理核心类 (DSP Logic)
# ==========================================
class AudioProcessor:
    def __init__(self):
        self.sr = 16000  
        self.y_orig = None
        self.y_processed = None

    def load_audio(self, filepath):
        y, sr = librosa.load(filepath, sr=self.sr)
        y = librosa.util.normalize(y)
        self.y_orig = y
        self.y_processed = None # 加载新音频时清空旧的处理结果

    def export_audio(self, filepath):
        """将处理后的音频导出保存"""
        if self.y_processed is not None:
            sf.write(filepath, self.y_processed, self.sr)

    def modify_lpc_poles(self, a_orig, target_f1, target_f2, strength):
        roots = np.roots(a_orig)
        complex_roots = roots[np.iscomplex(roots)]
        real_roots = roots[np.isreal(roots)]
        pos_roots = complex_roots[np.imag(complex_roots) > 0]
        
        freqs = np.angle(pos_roots) * self.sr / (2 * np.pi)
        mags = np.abs(pos_roots)
        
        sort_idx = np.argsort(freqs)
        sorted_freqs = freqs[sort_idx]
        sorted_mags = mags[sort_idx]
        
        valid_idx = np.where((sorted_freqs > 150) & (sorted_freqs < 3000))[0]
        
        if len(valid_idx) >= 2:
            idx1, idx2 = valid_idx[0], valid_idx[1]
            orig_f1, orig_f2 = sorted_freqs[idx1], sorted_freqs[idx2]
            
            sorted_freqs[idx1] = orig_f1 + (target_f1 - orig_f1) * strength
            sorted_freqs[idx2] = orig_f2 + (target_f2 - orig_f2) * strength
            
            # 强制收紧带宽，保证咬字锐利
            target_mag1 = np.exp(-np.pi * 50 / self.sr)
            target_mag2 = np.exp(-np.pi * 80 / self.sr)
            
            sorted_mags[idx1] = sorted_mags[idx1] + (target_mag1 - sorted_mags[idx1]) * strength
            sorted_mags[idx2] = sorted_mags[idx2] + (target_mag2 - sorted_mags[idx2]) * strength
            
        sorted_mags = np.clip(sorted_mags, 0, 0.995)
        new_pos_roots = sorted_mags * np.exp(1j * 2 * np.pi * sorted_freqs / self.sr)
        new_complex_roots = np.concatenate((new_pos_roots, np.conj(new_pos_roots)))
        all_new_roots = np.concatenate((new_complex_roots, real_roots))
        
        return np.poly(all_new_roots).real

    def process(self, target_f1, target_f2, strength):
        if self.y_orig is None:
            return

        y = self.y_orig
        pre_coef = 0.97
        y_pre = librosa.effects.preemphasis(y, coef=pre_coef)
        
        frame_len = int(0.025 * self.sr)
        hop_len = frame_len // 4 
        window = np.hanning(frame_len)
        lpc_order = int(self.sr / 1000) + 6  
        
        pad_length = frame_len - (len(y_pre) % hop_len)
        y_pre = np.pad(y_pre, (0, pad_length), mode='constant')
        
        y_out = np.zeros_like(y_pre)
        window_sum = np.zeros_like(y_pre)
        
        for i in range(0, len(y_pre) - frame_len + 1, hop_len):
            frame = y_pre[i : i + frame_len]
            frame_win = frame * window
            
            if np.sum(frame_win**2) < 1e-6:
                y_out[i : i + frame_len] += frame * (window ** 2)
                window_sum[i : i + frame_len] += window ** 2
                continue
            
            try:
                a_orig = librosa.lpc(frame_win, order=lpc_order)
                a_target = self.modify_lpc_poles(a_orig, target_f1, target_f2, strength)
                residual = signal.lfilter(a_orig, [1.0], frame_win)
                frame_synth = signal.lfilter([1.0], a_target, residual)
            except Exception:
                frame_synth = frame_win

            frame_synth = frame_synth * window
            
            energy_orig = np.sum(frame_win**2)
            energy_synth = np.sum(frame_synth**2)
            if energy_synth > 1e-10:
                frame_synth *= np.clip(np.sqrt(energy_orig / energy_synth), 0.1, 8.0)
            
            y_out[i : i + frame_len] += frame_synth
            window_sum[i : i + frame_len] += window ** 2

        mask = window_sum > 1e-8
        y_out[mask] /= window_sum[mask]
        y_out = y_out[:len(y)]
        
        y_out = signal.lfilter([1.0],[1.0, -pre_coef], y_out)
        self.y_processed = librosa.util.normalize(y_out) * np.max(np.abs(self.y_orig))

    def play_orig(self):
        if self.y_orig is not None:
            sd.stop()
            sd.play(self.y_orig, self.sr)

    def play_processed(self):
        if self.y_processed is not None:
            sd.stop()
            sd.play(self.y_processed, self.sr)
            
    def stop_audio(self):
        sd.stop()


# ==========================================
# 2. UI 组件: 元音图
# ==========================================
class VowelChartWidget(QWidget):
    formants_changed = pyqtSignal(float, float)

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setMinimumSize(540, 400)
        self.setCursor(Qt.CrossCursor) 
        
        self.f2_max, self.f2_min = 2600, 540
        self.f1_min, self.f1_max = 250, 1000
        
        self.current_f1 = 800
        self.current_f2 = 1400
        self.dot_radius = 8
        self.is_dragging = False

        self.vowels = {
            'i': (260, 2400), 'y': (260, 2000), 'ɯ': (260, 1300), 'u': (260, 700),
            'I': (350, 2100), 'ʊ': (350, 1000),
            'e': (400, 2250), 'ø': (400, 1850), 'ɵ': (400, 1400), 'ɤ': (400, 1100), 'o': (400, 800),
            'ə': (500, 1400),
            'ɛ': (600, 1950), 'œ': (600, 1600), 'ʌ': (600, 1100), 'ɔ': (600, 850),
            '(æ)': (700, 1600), 'a': (850, 1400), 'ɑ': (850, 1050), '(ɒ)': (850, 800)
        }
        self.lines = [['i', 'e', 'ɛ', 'a'],['y', 'ø', 'œ'],['ɯ', 'ɤ', 'ʌ', 'ɑ'],['u', 'o', 'ɔ', '(ɒ)'],['i', 'y'], ['e', 'ø'],['ɛ', 'œ'],['ɯ', 'u'], ['ɤ', 'o'],['ʌ', 'ɔ'],['ɑ', '(ɒ)']]

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.Antialiasing)
        width, height = self.width(), self.height()
        
        painter.fillRect(0, 0, width, height, QColor("#fdfdfd"))
        
        painter.setPen(QPen(QColor("#e0e0e0"), 1))
        for i in range(1, 15):
            painter.drawLine(0, int(i*height/10), width, int(i*height/10))
            painter.drawLine(int(i*width/15), 0, int(i*width/15), height)

        painter.setPen(QPen(QColor("#cccccc"), 2))
        for line_seq in self.lines:
            for i in range(len(line_seq)-1):
                f1_start, f2_start = self.vowels[line_seq[i]]
                f1_end, f2_end = self.vowels[line_seq[i+1]]
                x1, y1 = self.f_to_pos(f1_start, f2_start)
                x2, y2 = self.f_to_pos(f1_end, f2_end)
                painter.drawLine(int(x1), int(y1), int(x2), int(y2))

        painter.setPen(QPen(Qt.black))
        font = QFont("Microsoft YaHei", 16, QFont.Bold)
        painter.setFont(font)
        for v, (f1, f2) in self.vowels.items():
            x, y = self.f_to_pos(f1, f2)
            painter.drawText(int(x) - 10, int(y) + 8, v)

        painter.setPen(QPen(Qt.gray, 2))
        painter.setFont(QFont("Arial", 10))
        painter.drawText(10, 20, f"2600")
        painter.drawText(width//2 - 20, 20, f"F2 (Hz)")
        painter.drawText(width - 30, 20, f"540")
        painter.drawText(width - 50, 40, f"250")
        painter.drawText(width - 50, height//2, f"F1 (Hz)")
        painter.drawText(width - 50, height - 10, f"1000")

        x, y = self.f_to_pos(self.current_f1, self.current_f2)
        painter.setBrush(QBrush(QColor("#9b59b6")))
        painter.setPen(QPen(Qt.black, 1))
        painter.drawEllipse(QPointF(x, y), self.dot_radius, self.dot_radius)

    def f_to_pos(self, f1, f2):
        x = self.width() * (self.f2_max - f2) / (self.f2_max - self.f2_min)
        y = self.height() * (f1 - self.f1_min) / (self.f1_max - self.f1_min)
        return x, y

    def pos_to_f(self, x, y):
        f2 = self.f2_max - (x / self.width()) * (self.f2_max - self.f2_min)
        f1 = self.f1_min + (y / self.height()) * (self.f1_max - self.f1_min)
        return np.clip(f1, self.f1_min, self.f1_max), np.clip(f2, self.f2_min, self.f2_max)

    def mousePressEvent(self, event):
        if event.button() == Qt.LeftButton:
            f1, f2 = self.pos_to_f(event.x(), event.y())
            self.current_f1, self.current_f2 = f1, f2
            self.is_dragging = True
            self.update()
            self.formants_changed.emit(f1, f2)

    def mouseMoveEvent(self, event):
        if self.is_dragging:
            f1, f2 = self.pos_to_f(event.x(), event.y())
            self.current_f1, self.current_f2 = f1, f2
            self.update()
            self.formants_changed.emit(f1, f2)

    def mouseReleaseEvent(self, event):
        self.is_dragging = False

# ==========================================
# 3. 自定义滑块
# ==========================================
class JumpSlider(QSlider):
    def mousePressEvent(self, event):
        if event.button() == Qt.LeftButton:
            val = self.minimum() + (self.maximum() - self.minimum()) * event.x() / self.width()
            self.setValue(int(val))
        super().mousePressEvent(event)


# ==========================================
# 4. 主窗口 (支持拖放)
# ==========================================
class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("元音合成器")
        self.resize(750, 650)
        
        # 开启整个窗口的拖放支持
        self.setAcceptDrops(True)
        
        self.dsp = AudioProcessor()
        self.current_strength = 0.95
        self.params_changed = True 
        
        self.init_ui()
        self.apply_stylesheet()
        
        # 快捷键
        self.shortcut_space = QShortcut(QKeySequence(Qt.Key_Space), self)
        self.shortcut_space.activated.connect(self.handle_spacebar)

    def init_ui(self):
        main_widget = QWidget()
        self.setCentralWidget(main_widget)
        layout = QVBoxLayout(main_widget)

        # 顶部文件状态指示
        self.file_label = QLabel("将音频文件拖入此窗口，或点击「加载」(.wav / .flac)")
        self.file_label.setAlignment(Qt.AlignCenter)
        self.file_label.setStyleSheet("color: #7f8c8d; font-weight: bold; margin-bottom: 5px; font-size: 14px;")
        layout.addWidget(self.file_label)

        self.chart = VowelChartWidget()
        self.chart.formants_changed.connect(self.on_formants_changed)
        layout.addWidget(self.chart)

        self.info_label = QLabel(f"目标共振峰: F1={self.chart.current_f1:.0f} Hz, F2={self.chart.current_f2:.0f} Hz")
        self.info_label.setAlignment(Qt.AlignCenter)
        self.info_label.setFont(QFont("Microsoft YaHei", 12))
        layout.addWidget(self.info_label)

        slider_layout = QHBoxLayout()
        slider_layout.addWidget(QLabel("形变强度:"))
        self.slider = JumpSlider(Qt.Horizontal)
        self.slider.setRange(0, 100)
        self.slider.setValue(int(self.current_strength * 100))
        self.slider.valueChanged.connect(self.on_slider_changed)
        self.slider.setCursor(Qt.PointingHandCursor)
        slider_layout.addWidget(self.slider)
        
        self.strength_label = QLabel(f"{self.current_strength:.2f}")
        self.strength_label.setMinimumWidth(30)
        slider_layout.addWidget(self.strength_label)
        layout.addLayout(slider_layout)

        # 按钮控制区
        btn_layout = QHBoxLayout()
        
        self.btn_load = QPushButton("📂 加载音频")
        self.btn_load.clicked.connect(self.btn_load_clicked)
        self.btn_load.setCursor(Qt.PointingHandCursor)
        btn_layout.addWidget(self.btn_load)
        
        self.btn_play_orig = QPushButton("▶️ 原声")
        self.btn_play_orig.clicked.connect(self.dsp.play_orig)
        self.btn_play_orig.setCursor(Qt.PointingHandCursor)
        btn_layout.addWidget(self.btn_play_orig)
        
        self.btn_process = QPushButton("🚀 处理并播放 (Space)")
        self.btn_process.setObjectName("btn_process") 
        self.btn_process.clicked.connect(self.process_and_play)
        self.btn_process.setCursor(Qt.PointingHandCursor)
        btn_layout.addWidget(self.btn_process)
        
        self.btn_stop = QPushButton("⏹️ 停止")
        self.btn_stop.clicked.connect(self.dsp.stop_audio)
        self.btn_stop.setCursor(Qt.PointingHandCursor)
        btn_layout.addWidget(self.btn_stop)

        # 新增导出按钮
        self.btn_export = QPushButton("💾 导出音频")
        self.btn_export.setObjectName("btn_export")
        self.btn_export.clicked.connect(self.export_audio)
        self.btn_export.setCursor(Qt.PointingHandCursor)
        btn_layout.addWidget(self.btn_export)

        layout.addLayout(btn_layout)

    def apply_stylesheet(self):
        self.setStyleSheet("""
            QWidget { font-family: "Microsoft YaHei", Arial; }
            QPushButton {
                font-size: 13px; padding: 10px; border-radius: 6px;
                background-color: #ecf0f1; border: 1px solid #bdc3c7;
            }
            QPushButton:hover { background-color: #e0e6ed; }
            QPushButton:pressed { background-color: #d5dbdb; }
            #btn_process {
                background-color: #9b59b6; color: white; font-weight: bold; border: none;
            }
            #btn_process:hover { background-color: #8e44ad; }
            #btn_process:disabled { background-color: #bdc3c7; color: #7f8c8d; }
            
            #btn_export {
                background-color: #27ae60; color: white; font-weight: bold; border: none;
            }
            #btn_export:hover { background-color: #2ecc71; }
            
            QSlider::groove:horizontal { border: 1px solid #bbb; background: white; height: 12px; border-radius: 6px; }
            QSlider::sub-page:horizontal { background: #9b59b6; border: 1px solid #777; height: 12px; border-radius: 6px; }
            QSlider::handle:horizontal { background: #ffffff; border: 2px solid #9b59b6; width: 20px; margin-top: -6px; margin-bottom: -6px; border-radius: 10px; }
            QSlider::handle:horizontal:hover { background: #f4e8fa; }
        """)

    # ---------------- 拖放事件支持 (Drag & Drop) ----------------
    def dragEnterEvent(self, event):
        """当文件拖入窗口范围时触发"""
        if event.mimeData().hasUrls():
            urls = event.mimeData().urls()
            if urls and urls[0].isLocalFile():
                ext = os.path.splitext(urls[0].toLocalFile())[1].lower()
                # 限制允许拖入的后缀
                if ext in ['.wav', '.flac', '.ogg', '.mp3']:
                    event.acceptProposedAction()
                    return
        event.ignore()

    def dropEvent(self, event):
        """当文件在窗口内松开鼠标时触发"""
        filepath = event.mimeData().urls()[0].toLocalFile()
        self.load_audio_from_path(filepath)

    # ---------------- 文件加载与导出 ----------------
    def btn_load_clicked(self):
        filepath, _ = QFileDialog.getOpenFileName(self, "选择音频文件", "", "音频文件 (*.wav *.flac *.mp3)")
        if filepath:
            self.load_audio_from_path(filepath)

    def load_audio_from_path(self, filepath):
        """处理具体加载逻辑"""
        try:
            self.dsp.load_audio(filepath)
            filename = os.path.basename(filepath)
            self.file_label.setText(f"🎵 已加载音频: {filename}")
            self.file_label.setStyleSheet("color: #27ae60; font-weight: bold; margin-bottom: 5px; font-size: 14px;")
            self.params_changed = True 
        except Exception as e:
            QMessageBox.critical(self, "错误", f"无法加载音频:\n{e}")

    def export_audio(self):
        """导出处理后的音频"""
        if self.dsp.y_processed is None:
            QMessageBox.warning(self, "提示", "您还没处理音频呢，请先点击「处理并播放」生成音频再导出！")
            return
            
        filepath, _ = QFileDialog.getSaveFileName(self, "保存音频", "Vowel_Processed.wav", "WAV 文件 (*.wav)")
        if filepath:
            try:
                self.dsp.export_audio(filepath)
                QMessageBox.information(self, "成功", f"音频已成功导出到:\n{filepath}")
            except Exception as e:
                QMessageBox.critical(self, "导出失败", f"导出过程中发生错误:\n{e}")

    # ---------------- 播放与交互 ----------------
    def on_formants_changed(self, f1, f2):
        self.info_label.setText(f"目标共振峰: F1={f1:.0f} Hz, F2={f2:.0f} Hz")
        self.params_changed = True

    def on_slider_changed(self, value):
        self.current_strength = value / 100.0
        self.strength_label.setText(f"{self.current_strength:.2f}")
        self.params_changed = True

    def handle_spacebar(self):
        if self.dsp.y_orig is None:
            QMessageBox.warning(self, "提示", "请先加载一段音频！")
            return
        if not self.btn_process.isEnabled():
            return
        if self.params_changed or self.dsp.y_processed is None:
            self.process_and_play()
        else:
            self.dsp.play_processed()

    def process_and_play(self):
        if self.dsp.y_orig is None:
            QMessageBox.warning(self, "提示", "请先拖入或加载一段音频！")
            return
            
        self.btn_process.setEnabled(False)
        self.btn_process.setText("⚙️ 正在运算...")
        QApplication.setOverrideCursor(Qt.WaitCursor) 
        QApplication.processEvents()
        
        try:
            self.dsp.process(self.chart.current_f1, self.chart.current_f2, self.current_strength)
            self.params_changed = False 
            self.dsp.play_processed()
        except Exception as e:
            QMessageBox.critical(self, "运算错误", f"处理时发生错误:\n{e}")
        finally:
            QApplication.restoreOverrideCursor() 
            self.btn_process.setEnabled(True)
            self.btn_process.setText("🚀 处理并播放 (Space)")

if __name__ == "__main__":
    app = QApplication(sys.argv)
    window = MainWindow()
    window.show()
    sys.exit(app.exec_())