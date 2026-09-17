<template>
  <view class="app-container">
    <!-- 1. 顶部 HUD Header -->
    <view class="hud-header">
      <view class="device-brand">
        <view class="status-dot" :class="{ online: isConnected }"></view>
        <view class="brand-text">
          <text class="title">ESP32-C3 SMART MOUSE</text>
          <text class="subtitle">模式 3 专属 128-bit 调参终端</text>
        </view>
      </view>
      <button
        class="connect-btn"
        :class="{ active: isConnected }"
        @click="toggleConnect"
      >
        {{ isConnected ? '断开设备' : '搜寻并连接 [SmartMouse-Config]' }}
      </button>
    </view>

    <!-- 2. 核心参数实时 HUD 仪表面板 -->
    <view class="hud-card grid-3">
      <view class="hud-stat-item">
        <text class="stat-label">AIR DPI</text>
        <text class="stat-value highlight-blue">{{ dpi * 10 }}<text class="unit">%</text></text>
      </view>
      <view class="hud-stat-item">
        <text class="stat-label">TOUCH DPI</text>
        <text class="stat-value highlight-green">{{ wheelDpi * 10 }}<text class="unit">%</text></text>
      </view>
      <view class="hud-stat-item">
        <text class="stat-label">DEADZONE</text>
        <text class="stat-value highlight-purple">{{ deadzone }}<text class="unit">px</text></text>
      </view>
    </view>

    <!-- 3. 参数调节面板 -->
    <view class="hud-card control-panel" :class="{ disabled: !isConnected }">
      <view class="panel-header">
        <text class="panel-title">🎛️ 姿态与控制参数重构</text>
        <text class="panel-tag">模式 3 专属位点</text>
      </view>

      <!-- 飞鼠姿态 DPI -->
      <view class="slider-group">
        <view class="slider-header">
          <text class="slider-label">飞鼠姿态灵敏度 (Air DPI)</text>
          <text class="slider-val highlight-blue">{{ dpi }} 档 / {{ dpi * 10 }}%</text>
        </view>
        <slider
          :value="dpi"
          min="1"
          max="10"
          step="1"
          activeColor="#00d2ff"
          backgroundColor="#1a233a"
          block-color="#00d2ff"
          block-size="18"
          :disabled="!isConnected"
          @changing="onDpiChange"
          @change="onDpiChange"
        />
        <view class="quick-preset">
          <text class="preset-btn" @click="setDpiQuick(2)">低灵敏 (20%)</text>
          <text class="preset-btn" @click="setDpiQuick(5)">标准 (50%)</text>
          <text class="preset-btn" @click="setDpiQuick(9)">高灵敏 (90%)</text>
        </view>
      </view>

      <!-- 触控/滚轮 DPI -->
      <view class="slider-group">
        <view class="slider-header">
          <text class="slider-label">触控/滚轮灵敏度 (Touch DPI)</text>
          <text class="slider-val highlight-green">{{ wheelDpi }} 档 / {{ wheelDpi * 10 }}%</text>
        </view>
        <slider
          :value="wheelDpi"
          min="1"
          max="10"
          step="1"
          activeColor="#00e676"
          backgroundColor="#1a233a"
          block-color="#00e676"
          block-size="18"
          :disabled="!isConnected"
          @changing="onWheelDpiChange"
          @change="onWheelDpiChange"
        />
        <view class="quick-preset">
          <text class="preset-btn" @click="setWheelQuick(2)">慢速 (20%)</text>
          <text class="preset-btn" @click="setWheelQuick(5)">标准 (50%)</text>
          <text class="preset-btn" @click="setWheelQuick(8)">快速 (80%)</text>
        </view>
      </view>

      <!-- 滤波死区 -->
      <view class="slider-group">
        <view class="slider-header">
          <text class="slider-label">整型滤波死区 (Deadzone)</text>
          <text class="slider-val highlight-purple">{{ deadzone }} px</text>
        </view>
        <slider
          :value="deadzone"
          min="0"
          max="10"
          step="1"
          activeColor="#9c27b0"
          backgroundColor="#1a233a"
          block-color="#9c27b0"
          block-size="18"
          :disabled="!isConnected"
          @changing="onDeadzoneChange"
          @change="onDeadzoneChange"
        />
      </view>
    </view>

    <!-- 4. 终端控制台日志 -->
    <view class="hud-card console-panel">
      <view class="console-header">
        <view class="console-title">
          <text class="terminal-icon">>_</text>
          <text class="title-text">GATT 通信日志终端</text>
        </view>
        <text class="clear-btn" @click="clearLogs">清空</text>
      </view>
      <scroll-view scroll-y class="terminal-body" :scroll-top="scrollTop">
        <view v-for="(item, index) in logs" :key="index" class="terminal-row">
          <text class="time">[{{ item.time }}]</text>
          <text :class="['msg', item.type]">{{ item.msg }}</text>
        </view>
      </scroll-view>
    </view>
  </view>
</template>

<script>
export default {
  data() {
    return {
      isConnected: false,
      deviceId: '',
      targetServiceId: '12345678-1234-5678-1234-56789abcdef0',
      targetCharId: '12345678-1234-5678-1234-56789abcdef1',
      dpi: 5,        // 飞鼠 DPI 档位 (1~10)
      wheelDpi: 5,   // 触控 DPI 档位 (1~10)
      deadzone: 2,   // 死区像素 (0~10)
      logs: [],
      scrollTop: 0,
      sendTimer: null,
      isSearching: false
    };
  },
  mounted() {
    this.addLog('SYSTEM READY // 请长按 GPIO 10 键 2 秒进入模式 3', 'info');
  },
  methods: {
    addLog(msg, type = 'info') {
      const time = new Date().toLocaleTimeString('zh-CN', { hour12: false });
      this.logs.push({ time, msg, type });
      this.$nextTick(() => {
        this.scrollTop = this.logs.length * 40;
      });
    },
    clearLogs() {
      this.logs = [];
    },
    toggleConnect() {
      if (this.isConnected) {
        uni.closeBLEConnection({
          deviceId: this.deviceId,
          complete: () => {
            this.isConnected = false;
            this.addLog('BLE // 通道已主动断开', 'warn');
            uni.closeBluetoothAdapter();
          }
        });
        return;
      }

      this.addLog('BLE // 初始化底层蓝牙适配器...', 'info');
      uni.closeBluetoothAdapter({
        complete: () => {
          uni.openBluetoothAdapter({
            success: () => {
              this.startDiscovery();
            },
            fail: (err) => {
              this.addLog('ERROR // 蓝牙开启失败: ' + JSON.stringify(err), 'error');
            }
          });
        }
      });
    },
    startDiscovery() {
      this.addLog('BLE // 搜寻 [SmartMouse-Config]...', 'info');
      this.isSearching = true;

      uni.startBluetoothDevicesDiscovery({
        allowDuplicatesKey: false,
        success: () => {
          uni.onBluetoothDeviceFound((res) => {
            res.devices.forEach(device => {
              const name = (device.name || device.localName || '').trim();
              const devId = device.deviceId || '';

              if (name.includes('SmartMouse-Config') && this.isSearching) {
                this.isSearching = false;
                this.addLog(`MATCHED // 命中模式 3 设备: ${name}`, 'success');
                uni.stopBluetoothDevicesDiscovery();
                this.connectDevice(devId);
              }
            });
          });
        },
        fail: (err) => {
          this.addLog('ERROR // 搜寻异常: ' + JSON.stringify(err), 'error');
        }
      });
    },
    connectDevice(deviceId) {
      this.deviceId = deviceId;
      this.addLog('GATT // 建立物理层 ACL 通道...', 'info');

      uni.createBLEConnection({
        deviceId,
        success: () => {
          this.addLog('GATT // 握手成功，获取 128-bit 专用调参服务...', 'info');
          setTimeout(() => {
            uni.getBLEDeviceServices({
              deviceId,
              success: (res) => {
                let service = res.services.find(s => s.uuid.toLowerCase().includes('12345678'));
                if (service) {
                  this.targetServiceId = service.uuid;
                  this.getCharacteristics(deviceId, this.targetServiceId);
                } else {
                  this.addLog('ERROR // 未检索到配置服务', 'error');
                }
              },
              fail: (err) => {
                this.addLog('ERROR // 获取服务失败: ' + JSON.stringify(err), 'error');
              }
            });
          }, 800);
        },
        fail: (err) => {
          this.addLog('ERROR // 连接超时: ' + JSON.stringify(err), 'error');
        }
      });
    },
    getCharacteristics(deviceId, serviceId) {
      uni.getBLEDeviceCharacteristics({
        deviceId,
        serviceId,
        success: (res) => {
          let char = res.characteristics.find(c => c.uuid.toLowerCase().includes('12345678'));
          if (char) {
            this.targetCharId = char.uuid;
            this.isConnected = true;
            this.addLog('CONNECTED // 128-bit 模式 3 调参通道建立成功！', 'success');
          } else {
            this.addLog('ERROR // 未检索到特征值位点', 'error');
          }
        },
        fail: (err) => {
          this.addLog('ERROR // 获取特征值失败: ' + JSON.stringify(err), 'error');
        }
      });
    },
    setDpiQuick(val) {
      if (!this.isConnected) return;
      this.dpi = val;
      this.sendParamsDebounced();
    },
    setWheelQuick(val) {
      if (!this.isConnected) return;
      this.wheelDpi = val;
      this.sendParamsDebounced();
    },
    onDpiChange(e) {
      this.dpi = e.detail.value;
      this.sendParamsDebounced();
    },
    onWheelDpiChange(e) {
      this.wheelDpi = e.detail.value;
      this.sendParamsDebounced();
    },
    onDeadzoneChange(e) {
      this.deadzone = e.detail.value;
      this.sendParamsDebounced();
    },
    sendParamsDebounced() {
      if (this.sendTimer) clearTimeout(this.sendTimer);
      this.sendTimer = setTimeout(() => {
        this.sendParams();
      }, 50);
    },
    sendParams() {
      if (!this.isConnected) return;

      const header = 0xA5;
      const cmd = 0x01;
      const airDpiVal = parseInt(this.dpi);
      const wheelDpiVal = parseInt(this.wheelDpi);
      const deadzoneVal = parseInt(this.deadzone);

      const buffer = new ArrayBuffer(5);
      const dataView = new DataView(buffer);

      dataView.setUint8(0, header);
      dataView.setUint8(1, cmd);
      dataView.setUint8(2, airDpiVal);
      dataView.setUint8(3, wheelDpiVal);
      dataView.setUint8(4, deadzoneVal);

      uni.writeBLECharacteristicValue({
        deviceId: this.deviceId,
        serviceId: this.targetServiceId,
        characteristicId: this.targetCharId,
        value: buffer,
        success: () => {
          this.addLog(`TX ➔ [0xA5] Air:${airDpiVal*10}% | Touch:${wheelDpiVal*10}% | Deadzone:${deadzoneVal}px`, 'success');
        },
        fail: (err) => {
          this.addLog(`TX_FAIL // 下发失败: ${JSON.stringify(err)}`, 'error');
        }
      });
    }
  }
};
</script>

<style>
/* 赛博暗黑 HUD 风格 */
.app-container {
  padding: 16px;
  background-color: #0b0e14;
  min-height: 100vh;
  box-sizing: border-box;
  color: #c5cddb;
  font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif;
}

.hud-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  background: #121824;
  padding: 14px 16px;
  border-radius: 12px;
  border: 1px solid #1e293b;
  margin-bottom: 14px;
}

.device-brand {
  display: flex;
  align-items: center;
  gap: 12px;
}

.status-dot {
  width: 10px;
  height: 10px;
  border-radius: 50%;
  background-color: #ff5252;
  box-shadow: 0 0 8px #ff5252;
}

.status-dot.online {
  background-color: #00e676;
  box-shadow: 0 0 8px #00e676;
}

.brand-text .title {
  display: block;
  font-size: 14px;
  font-weight: 700;
  color: #ffffff;
  letter-spacing: 0.5px;
}

.brand-text .subtitle {
  font-size: 10px;
  color: #64748b;
}

.connect-btn {
  background: #1e293b;
  color: #00d2ff;
  border: 1px solid #00d2ff40;
  font-size: 12px;
  font-weight: 600;
  border-radius: 8px;
  padding: 0 14px;
  height: 34px;
  line-height: 34px;
  margin: 0;
}

.connect-btn.active {
  background: #ff525220;
  color: #ff5252;
  border-color: #ff525260;
}

.hud-card {
  background: #121824;
  border-radius: 12px;
  border: 1px solid #1e293b;
  padding: 16px;
  margin-bottom: 14px;
}

.hud-card.disabled {
  opacity: 0.4;
  pointer-events: none;
}

.grid-3 {
  display: grid;
  grid-template-columns: 1fr 1fr 1fr;
  gap: 10px;
  padding: 12px;
}

.hud-stat-item {
  display: flex;
  flex-direction: column;
  align-items: center;
}

.stat-label {
  font-size: 10px;
  color: #64748b;
  font-weight: 600;
  margin-bottom: 4px;
}

.stat-value {
  font-size: 18px;
  font-weight: 700;
}

.stat-value .unit {
  font-size: 11px;
  margin-left: 2px;
  font-weight: 400;
}

.highlight-blue { color: #00d2ff; }
.highlight-green { color: #00e676; }
.highlight-purple { color: #b388ff; }

.panel-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 16px;
  border-bottom: 1px solid #1e293b;
  padding-bottom: 10px;
}

.panel-title {
  font-size: 14px;
  font-weight: 600;
  color: #f1f5f9;
}

.panel-tag {
  font-size: 10px;
  background: #1e293b;
  color: #94a3b8;
  padding: 2px 6px;
  border-radius: 4px;
}

.slider-group {
  margin-bottom: 18px;
}

.slider-header {
  display: flex;
  justify-content: space-between;
  font-size: 12px;
  margin-bottom: 6px;
}

.slider-label {
  color: #94a3b8;
}

.slider-val {
  font-weight: 700;
}

.quick-preset {
  display: flex;
  gap: 8px;
  margin-top: 6px;
}

.preset-btn {
  font-size: 10px;
  background: #1a233a;
  color: #94a3b8;
  padding: 4px 8px;
  border-radius: 4px;
  border: 1px solid #283556;
}

.preset-btn:active {
  background: #00d2ff20;
  color: #00d2ff;
}

.console-panel {
  padding: 12px;
}

.console-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 8px;
}

.console-title {
  display: flex;
  align-items: center;
  gap: 6px;
}

.terminal-icon {
  color: #00e676;
  font-family: monospace;
  font-weight: 700;
}

.title-text {
  font-size: 12px;
  color: #94a3b8;
  font-weight: 600;
}

.clear-btn {
  font-size: 11px;
  color: #00d2ff;
}

.terminal-body {
  background-color: #080a0f;
  border-radius: 8px;
  padding: 10px;
  height: 130px;
  box-sizing: border-box;
  border: 1px solid #182232;
}

.terminal-row {
  font-size: 11px;
  line-height: 18px;
  font-family: monospace;
  display: flex;
  gap: 8px;
}

.terminal-row .time {
  color: #475569;
}

.terminal-row .msg.info { color: #cbd5e1; }
.terminal-row .msg.success { color: #00e676; }
.terminal-row .msg.warn { color: #ffb300; }
.terminal-row .msg.error { color: #ff5252; }
</style>
