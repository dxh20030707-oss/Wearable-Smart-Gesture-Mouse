# 🚀 ESP32-C3 智能飞鼠/触控一体鼠标项目阶段性总结报告

> **版本 v3.2 ｜ 更新日期 2026-09-22 ｜ 对应固件：三模式架构（模式 1 / 2 / 3）**
>
> **v3.2 变更**：修复 OLED 滚轮箭头方向判定错误——原用 `mouseWheelCount % 2` 判断方向（奇偶无法表达方向，同一方向连续滚动时箭头每格翻转），改为新增 `g_scroll_dir` 实时方向状态量（`mouse_mode.cpp:37`，左/右键按下置 `+1`/`-1`、松手置 `0`），`display_mode.cpp:75-81` 按「向上 / 向下 / 静止」三态渲染。编译通过（RAM 13.6% / Flash 36.4%）。
>
> **v3.1 变更**：EC11 旋钮硬件已拆除，模式 2 的全部编码器代码（中断、计数、旋钮调参、`KNOB_DEBOUNCE_MS`）连同 `PIN_M2_TIM_CH1/CH2` 引脚宏一并移除；同步清理死变量 `sensitivityList` / `currentSensIdx` / `is_ctrl_pressed` 与过时注释、OLED 文案（`M2: TOUCH & KNOB` → `M2: TOUCH PAD`）。清理后重新编译通过（RAM 13.6% / Flash 36.4%）。
>
> **v3.0 变更（相对 v2）**：两模式 → 三模式；GPIO 0/1 由 EC11 编码器改为**触摸按键 + 动力学平滑滚动**；新增**模式 3 手机 BLE 调参**与 **NVS 参数持久化**；模式切换由运行时热切换改为**落盘 + 重启**。
>
> 文档分工：`README.md` 面向外部展示，本报告面向内部复盘（架构事实 / 缺陷清单 / 演进规划）。
> 报告中的 `文件名:行号` 均对应当前版本，代码变动后请同步更新。

系统基于 **ESP32-C3** 主控，利用 PlatformIO + Arduino 框架开发。通过 GPIO 时分复用架构，将惯性传感器（MPU6050）、电容触控屏（FT6336U）、OLED 屏幕与触摸按键有机结合，实现三种工作模式的切换。

---

## 📌 一、架构现状梳理

### 1. 三模式系统架构

| 模式 | 枚举值 | BLE 身份 | 主外设 | 进入方式 | 退出方式 |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **模式 1 · 飞鼠** | `MODE_FLY_MOUSE = 0` | `ESP32-C3 SmartMouse`（标准 HID 鼠标） | MPU6050 + OLED | 开机恢复 / 短按 GPIO 10 | 短按 → 模式 2；长按 ≥1.5s → 模式 3 |
| **模式 2 · 电容触控** | `MODE_TOUCH_PAD = 1` | 同上（HID 鼠标） | FT6336U + OLED | 短按 GPIO 10 | 短按 → 模式 1；长按 ≥1.5s → 模式 3 |
| **模式 3 · App 调参** | `MODE_APP_CONFIG = 2` | `SmartMouse-Config`（自定义 128-bit GATT 服务） | OLED（状态显示） | 长按 GPIO 10 ≥1.5s | 长按 ≥1.5s → 模式 1（**此模式下短按无效**，见 `main.cpp:144`） |

**关键机制**：模式状态由 `g_current_mode`（`main.cpp:36`）单一变量承载，并在 `setup()`（`main.cpp:163`）与 `loop()`（`main.cpp:218`）两处分流。模式切换采用 **「写入 NVS → `ESP.restart()` 重启」**（`main.cpp:140-142`、`main.cpp:152-154`），以重启换取彻底的状态复位——这是规避「引脚复用残留 / 中断未解绑」类问题的最直接手段。

### 2. 引脚分配总览（GPIO 复用架构）

模式切换键（GPIO 10）为唯一固定功能引脚。**v3.1 起 GPIO 0/1 已不再于模式 2 中承担编码器功能（旋钮已拆除），模式 2 仅占用 GPIO 2/3。**

| 物理引脚 | 公共通道 | 模式 1：飞鼠正常 | 模式 1：飞鼠断控 | 模式 2：电容触控 |
| :--- | :--- | :--- | :--- | :--- |
| **GPIO 0** | — | 触摸左键（鼠标左键） | **触摸「向上滚动」键** | ⬜ **已释放**（原 EC11 A 相） |
| **GPIO 1** | — | 触摸右键（鼠标右键） | **触摸「向下滚动」键** | ⬜ **已释放**（原 EC11 B 相） |
| **GPIO 2** | — | 断控切换键（进入/退出滚轮） | 断控切换键 | 触控芯片中断 (INT) |
| **GPIO 3** | — | 飞鼠灵敏度环形调节键 | 滚轮灵敏度环形调节键 | 触控芯片复位 (RST) |
| **GPIO 8** | I2C SDA @400kHz | MPU6050 / OLED | MPU6050 / OLED | FT6336U / OLED |
| **GPIO 9** | I2C SCL @400kHz | MPU6050 / OLED | MPU6050 / OLED | FT6336U / OLED |
| **GPIO 10** | 模式切换触摸键 | 短按→M2 / 长按→M3 | 短按→M2 / 长按→M3 | 短按→M1 / 长按→M3 |

**触摸按键电气约定**（`mouse_mode.cpp:37-38`）：触摸模块输出为**点动高电平**，故 `TOUCH_PRESSED = HIGH`、`TOUCH_RELEASED = LOW`，引脚统一配置为 `pinMode(pin, INPUT)`（`mouse_mode.cpp:258-261`、`main.cpp:167`）。

> 💡 **可利用空间**：模式 2 释放了 GPIO 0/1，可复用为触控模式下的快捷功能（如中键、手势切换、灵敏度调节），见规划第 1 节。

### 3. 参数体系与 NVS 持久化

统一参数结构体 `SystemConfig`（`config.h:42`）把「档位」与「算法增益」解耦：

```cpp
struct SystemConfig {
    uint8_t air_dpi_level;      // 飞鼠灵敏度档位 (1~10)
    uint8_t touch_dpi_level;    // 触控/滚轮灵敏度档位 (1~10)
    uint8_t deadzone_px;        // 死区 (0~10)
    float   air_gain;           // 由档位映射的飞鼠算法增益
    float   touch_gain;         // 由档位映射的触控算法增益
};
```

**档位 → 增益映射**（`main.cpp:48-52`）：

| 目标 | 公式 | 取值范围 |
| :--- | :--- | :--- |
| 飞鼠增益 `air_gain` | `0.2 + air_dpi_level × 0.18` | 0.38 ~ 2.00 |
| 触控增益 `touch_gain` | `0.5 + touch_dpi_level × 0.25` | 0.75 ~ 3.00 |
| OLED 显示用百分比 | `global_sens_percent = air_dpi_level × 10`；`global_wheel_sens_percent = touch_dpi_level × 10` | 10 ~ 100 |

**NVS 持久化**（`main.cpp:56-78`，命名空间 `mouse_cfg`）：

| Key | 类型 | 默认值 | 说明 |
| :--- | :--- | :--- | :--- |
| `air_dpi` | UChar | 5 | 飞鼠档位 1~10 |
| `touch_dpi` | UChar | 5 | 触控/滚轮档位 1~10 |
| `deadzone` | UChar | 2 | 死区 0~10 |
| `sys_mode` | UChar | 0 | **当前模式也随之落盘**，开机恢复（越界值回落为 0，`main.cpp:74`） |

**落盘入口共 4 处**：模式 3 的 App 下发（`main.cpp:99`）、模式切换（`main.cpp:140`、`main.cpp:152`）、模式 1 的灵敏度按键（`mouse_mode.cpp:223`、`mouse_mode.cpp:240`）。

### 4. 代码分布与文件明细

* **`include/config.h`**（59 行）
  引脚宏（I2C 13-14、模式 1 18-21、模式 2 24-25、模式切换 15）、模式枚举 `SystemMode`（35）、参数结构体 `SystemConfig`（42）、模式 3 的 `CONFIG_SERVICE_UUID` / `CONFIG_CHAR_UUID`（28-29）、三个 NVS 接口与 `extern g_cfg` / `g_current_mode`（51-52）声明。
* **`src/main.cpp`**（290 行）
  系统总入口与全局实体。参数映射与 NVS（`updateConfigGains` 47-54 / `saveConfigToNVS` 56 / `loadConfigFromNVS` 67）、**模式 3 的 GATT 服务与写回调**（`AppConfigCallbacks` 84、`ConfigServerCallbacks` 108）、**模式切换状态机**（`handleModeSwitch` 119-156）、`setup()` 三模式分支（163-210）、`loop()` 时间片调度与三套 OLED 刷新（218-288）。
* **`src/mouse_mode.cpp`**（327 行）｜模式 1 行为层
  触摸按键状态机 `updateNormalButtons:43-157`（正常模式→左右键；断控模式→触摸控上下滚动 + 20ms 动力学累加连发）、断控切换 `updateLockKeyLogic:162-189`、灵敏度环形调节 `updateSensKeyLogic:194-244`、开机 50 次静态零偏标定与光标链路 `initMouseMode:249` / `updateMouseMode:277`。
* **`src/touch_mode.cpp`**（329 行）｜模式 2 行为层
  FT6336U 双点 I2C 解析 `readFT6336U_Dual:28-79`；触控中断 `handleTouchINT_ISR:24`；初始化 `initTouchMode:113-138`；手势状态机 `updateTouchMode:140`（单指移动 / 单击双击 / 拖拽 / 双指缩放 / 双指持续滚动 / 双指轻击右键）。**v3.1 已移除全部 EC11 旋钮代码。**
* **`src/display_mode.cpp`**（213 行）｜OLED 渲染引擎
  模式 1 与模式 2 两套界面；依赖 `global_sens_percent` 等全局变量渲染。注意 `drawWheelConfigUI:170` 目前**无任何调用者**（见缺陷 C-8）。
* **`src/mpu6050_driver.cpp`**（100 行，本版未改动）｜MPU6050 寄存器级初始化与 14 字节原始数据流读取。
* **`src/imu_processing.c`**（50 行）｜姿态角解算接口，**仍为占位实现**。

### 5. 总体实现思路

核心思路仍为 **「时间片节流 + 状态机分流」**：

* **主循环时间片**：HID 派发窗口 15ms（`main.cpp:237`）、OLED 刷新 100ms（`main.cpp:256`）、循环尾 `delay(6)`（`main.cpp:287`）主动让出 CPU 给 NimBLE 后台线程。连接建立后额外等待 2000ms 才派发数据（`main.cpp:241`），避免配对新链路时的抖动。
* **参数单一真源**：所有灵敏度/死区以 `g_cfg` 为唯一真源，`updateConfigGains()` 统一换算为算法增益并同步显示变量；App（模式 3）与板载按键（模式 1）改的是同一个结构体，且都会落盘。
* **断控滚动**：**「触摸点击 + 动力学累加器」**——触摸瞬间发 1 格首包（零延迟触感），按住后以 20ms 为周期按灵敏度累加连发，松手立即清零刹车（`mouse_mode.cpp:87-156`）。速度公式：`speed = 0.05 + (wheel_sens% / 100) × 0.80`（`mouse_mode.cpp:132`）。
* **触控持续滚动**：位置/速度时钟触发，手指越过 20px 判定线且未抬起时，由内部定时器每 40ms 持续补发滚轮包，从机制上消除断续卡顿。
* **模式 2 的灵敏度调节入口**：旋钮移除后，触控/滚轮档位只能通过 **App（模式 3）** 或 **模式 1 断控状态下按 GPIO 3** 调整（`mouse_mode.cpp:218-222`）。

---

## ⚠️ 二、现有代码缺失及潜在缺陷检查

按严重度排序。**A 类为功能/寿命风险，建议优先处理。**

### A. 高优先级

1. **NVS 高频写入，存在 Flash 磨损风险**
   模式 1 长按灵敏度调节时，每 150ms 就调用一次 `saveConfigToNVS()`（`mouse_mode.cpp:210-223`）；短按调节同样立即落盘（`mouse_mode.cpp:240`）。ESP32 的 NVS 分区擦写寿命约 10 万次量级，按 150ms/次估算，**连续长按约 4 小时即可耗尽一个 NVS 页的擦写次数**。
   **建议**：改为「脏标记 + 松手/超时批量落盘」（例如松手后 1~2s 写一次），或至少加 10~30s 的落盘节流。

2. **模式 2 缺少灵敏度调节手段（v3.1 移除旋钮后的新缺口）**
   旋钮代码随硬件拆除后，模式 2 内已无任何调节灵敏度的入口（GPIO 0/1 释放、GPIO 3 被触控 RST 占用），用户必须切到模式 1 断控或模式 3 App 才能改触控档位。
   **建议**：把释放出来的 GPIO 0/1 复用为模式 2 的「档位 +/−」或「中键」，补齐板载调节能力（见规划第 1 节）。

### B. 中优先级

3. **`deadzone_px` 一个参数承担两种物理单位**
   输入侧死区为 `deadzone_px × 40`（原始陀螺仪 LSB 单位，`mouse_mode.cpp:296`），输出侧死区为 `deadzone_px`（像素单位，`mouse_mode.cpp:309-310`）；触控侧还用它做位移阈值（`touch_mode.cpp:222`）。三种语义随同一滑杆联动：在 ±500°/s 量程（65.5 LSB/(°/s)）下，`deadzone_px = 10` 相当于 **约 6.1 °/s 的输入死区**，足以吞掉慢速移动。
   **建议**：拆成「输入死区（LSB）」与「输出死区（px）」两个独立参数。

4. **触摸引脚使用 `INPUT` 且无上下拉，悬空即随机**
   `mouse_mode.cpp:258-261` 与 `main.cpp:167` 均配置为 `INPUT`（适配触摸模块推挽高电平）。若触摸模块未接线或接触不良，引脚悬空会使读数随机跳变，可能造成**误触发按键/模式切换**。
   **建议**：确认触摸模块输出为推挽后保留现状并在文档标注；或加入上电自检。

5. **模式 3 与 HID 互斥，且 App 写参无回读**
   模式 3 只注册自定义 GATT 服务（`main.cpp:183-199`），不启动 `BleMouse`，因此**进入调参模式后电脑鼠标即失联**，无法「边调边试」。同时该特征值仅开放 `READ/WRITE/WRITE_NR`，**没有 NOTIFY**，App 无法回读设备参数，OLED 是唯一反馈通道（`main.cpp:265-284`）。
   **建议**：① 评估「HID + 配置服务共存」；② 为特征值增加 NOTIFY，参数变更后主动上报。

6. **`CONFIG_BT_NIMBLE_MAX_CONNECTIONS=2` 与当前实现不匹配**
   `platformio.ini` 已放开双并发连接（注释写明「1 个给电脑鼠标，1 个给手机 App 调参」），但模式 3 下并未注册 HID 服务，该能力无使用场景，属于**提前预留但未落地**的配置。
   **建议**：随第 5 项一并落地，或暂时改回 1 以压缩内存占用。

7. **模式切换依赖 `ESP.restart()`，切换成本较高**
   长短按均以「落盘 + 重启」收尾（`main.cpp:140-142`、`152-154`），切换过程约 1s 内设备不可用、BLE 需重新连接；且长按需**松手后**才判定（`pressDuration` 在释放瞬间计算，`main.cpp:129-132`），手感上会多等一次抬手。
   **建议**：长按达到阈值即刻触发（不必等松手）；中长期以「软切换 + 状态复位」替代重启（见规划第 4 节）。

### C. 清理项（不影响功能，但影响可读性）

8. **`drawWheelConfigUI` 是死函数**：`display_mode.cpp:170` 定义、`display_mode.h:15` 声明，全项目**无任何调用者**。它在旋钮移除后更无使用可能，建议删除或明确标注为预留界面。
9. **速度注释数值偏差**：`mouse_mode.cpp:129-131` 注释「50% → 速度 0.35、约 57ms/格」，而按 `0.05 + 0.5 × 0.80 = 0.45`、20ms/周期计算，实际约 **0.45 / 44ms 一格**（10% 档注释 0.12，实际 0.13；100% 档 0.85 与实际一致）。注释为旧公式残留。
10. **`imu_processing.c` 仍为占位**：互补滤波/欧拉角解算未接入主链路，光标完全依赖陀螺仪原始值积分与死区处理（模式 1 只用 `data_ptr[5]/[6]` 两个陀螺仪轴）。
11. **显示层未接入参数体系**：`display_mode.cpp` 仍 `extern` 旧的 `global_sens_percent` / `global_wheel_sens_percent`，靠 `updateConfigGains()` 反向同步（`main.cpp:52-53`）间接渲染，属隐性耦合，建议改为直接接收 `g_cfg` 或显式传参。
12. **`mouseWheelCount` 现在只写不读**：`display_mode.cpp` 改用 `g_scroll_dir` 后，该计数器（`mouse_mode.cpp:18`）只在触摸滚动时累加、已无任何读取者。若确认不再需要「累计滚动格数」这一信息，建议连同 4 处 `+=` / `-=` 一并删除。

### ✅ 已修复项（v3.1 ~ v3.2）

| 原问题 | 处理 |
| :--- | :--- |
| **OLED 滚轮箭头方向判定错误**：`mouseWheelCount % 2` 无法表达方向，同一方向连续滚动时箭头每格翻转一次 | 新增 `g_scroll_dir`（`mouse_mode.cpp:37`）：左/右键按下置 `+1`/`-1`、松手与断控切换时置 `0`；`display_mode.cpp:75-81` 改为「向上 / 向下 / 静止」三态渲染 |
| 模式 2 的 EC11 中断缺少软件防抖（硬件无 RC 滤波、易反向误计数） | 旋钮硬件已拆除，**全部编码器代码删除**，问题消除 |
| `is_ctrl_pressed` 恒为 `false`，模式 2 的「旋钮发滚轮」分支不可达 | 随编码器代码一并删除该死分支与变量 |
| `sensitivityList` / `currentSensIdx` 死代码（零引用） | 已从 `main.cpp` 与 `touch_mode.cpp` 删除 |
| `display_mode.cpp` 注释与实现不符（「实时获取 EC11 滚轮编码器物理计数」） | 已更正为「断控滚轮计数（由触摸滚动逻辑累加）」 |
| OLED 模式 2 文案仍显示 `M2: TOUCH & KNOB`（暗示存在旋钮） | 已改为 `M2: TOUCH PAD` |
| `config.h:18-19` 引脚注释仍写「鼠标左键 / 编码器 A相」（易误导） | 已改为「触摸按键：鼠标左键（断控时=向上滚动）」等 |
| 模式 2 的灵敏度在旋钮移除后失去板载调节入口 | **未修复**，已列为缺陷 A-2 与规划第 1 节的「回填 GPIO 0/1」 |

---

## 🔮 三、未来优化计划与发展建议

### 1. 触控屏手势与释放引脚的回填

* **回填 GPIO 0/1**：把模式 2 释放出的两个引脚接上触摸按键，作为「触控档位 +/−」或「中键/返回」，补齐板载调节能力（对应缺陷 A-2）。
* **动态锚点中心缩放**：当前双指捏合（`distDelta`）固定发送中键+滚轮信号。可计算双指几何中心 $(X_c, Y_c)$，按中心位于屏幕左/右侧动态调整缩放包频率，使其更贴近 CAD / EDA 软件的直觉。
* **右键二次判定加固**：双指轻击右键目前仅靠 `<300ms` 时间差（`touch_mode.cpp:306`）。建议引入双指间距突变率过滤——若抬起瞬间两点距离大幅发散，说明是滑动的收尾动作，应剔除。
* **双指滚动速度阶梯**：持续滚动目前恒定 20px 触发 + 40ms 周期，可按双指位移量分级（小幅=精确滚动，大幅=快速翻页）。

### 2. 飞鼠与滚轮

* **虚拟飞轮（Flywheel）**：断控滚轮已实现「按住连发」，可继续叠加**惯性滑行**——检测到快速连击后，在松手后以指数衰减频率补发几帧滚轮包，模拟无阻尼滚轮手感。
* **单击/双击语义扩展**：GPIO 0/1 现为触摸按键，具备识别单击/双击的硬件基础，可扩展为「单击=上下滚一格、双击=翻页/中键、长按=连续滚动」，进一步减少对模式切换的依赖。
* **长按加速曲线可调**：当前连发速度为线性映射（`0.05 + sens% × 0.80`），可改为曲线映射或按「已按住时长」递增，兼顾近距精调与长文档快速浏览。

### 3. 参数系统与 App 联动

* **参数回读（NOTIFY）**：让 App 显示设备真实参数，替代「盲写 + 看 OLED」。
* **落盘节流与脏标记**：解决缺陷 A-1 的 Flash 磨损。
* **参数分组扩展**：在 `SystemConfig` 中增加滤镜系数（α）、DAMPING、连发周期等，使 App 成为完整的调试终端。
* **调参实时预览**：模式 3 下让 OLED 显示最近一次写入的参数值，便于现场确认。

### 4. 系统级重构

* **统一全局事件总线（Event Bus）**：把长按/短按/双击等按键事件抽象为独立事件，取代各模块内互相 `digitalRead` + 就地分流的写法（当前 `updateSensKeyLogic` 仍在内部用 `is_airmouse_locked` 切换语义），从根本上消除按键语义混用一类问题。
* **模式软切换（免重启）**：以「反初始化 + 重新初始化」的显式生命周期函数替代 `ESP.restart()`，保留 BLE 连接、缩短切换时间。
* **统一传感器抽象层**：为 IMU / 触控芯片定义统一接口，支持器件替换与单元测试（`imu_processing.c` 的占位状态应随之补齐或删除）。
* **代码清理**：按 C 类清单清理过时注释（`config.h:18-19`）、死函数（`drawWheelConfigUI`）与文档行号，保持文档与实现一致。

---

## 📎 附录：关键阈值速查表

| 类别 | 参数 | 值 | 位置 |
| :--- | :--- | :--- | :--- |
| 调度 | HID 派发窗口 | 15ms | `main.cpp:237` |
| 调度 | OLED 刷新 | 100ms | `main.cpp:256` |
| 调度 | 主循环让出 | delay(6) | `main.cpp:287` |
| 调度 | 连接后启动延迟 | 2000ms | `main.cpp:241` |
| 飞鼠 | 光标链路节流 | 12ms | `mouse_mode.cpp:286` |
| 飞鼠 | 低通滤波系数 α | 0.4（一阶 EMA） | `mouse_mode.cpp:26` |
| 飞鼠 | 位移阻尼 DAMPING | 120 | `mouse_mode.cpp:23` |
| 飞鼠 | 输入死区 | `deadzone_px × 40` LSB | `mouse_mode.cpp:296` |
| 飞鼠 | 输出死区 | `deadzone_px` px（末级归零） | `mouse_mode.cpp:309` |
| 飞鼠 | 开机零偏标定 | 50 次采样平均 | `mouse_mode.cpp:266` |
| 按键 | 左右键去抖 | 20ms | `mouse_mode.cpp:62/75` |
| 按键 | 断控切换去抖 | 50ms | `mouse_mode.cpp:168` |
| 按键 | 灵敏度长按触发 / 自动步进 | 400ms / 150ms | `mouse_mode.cpp:210/212` |
| 按键 | 模式键短按 / 长按 | ≥50ms / ≥1500ms | `main.cpp:144/132` |
| 滚轮 | 断控连发 tick | 20ms | `mouse_mode.cpp:125` |
| 滚轮 | 连发速度公式 | `0.05 + sens% × 0.80` | `mouse_mode.cpp:132` |
| 触控 | 滑动判定 / 缩放判定 | 10px / 6px | `touch_mode.cpp:105/107` |
| 触控 | 单击窗口 / 双指右键窗口 | 250ms / <300ms | `touch_mode.cpp:106/306` |
| 触控 | 双指冷却 | 150ms | `touch_mode.cpp:108` |
| 触控 | 持续滚动触发 / 周期 | 20px / 40ms | `touch_mode.cpp:110/111` |
| 触控 | 单指位移阈值 / 增益 | `g_cfg.deadzone_px` / `g_cfg.touch_gain` | `touch_mode.cpp:222/223` |
| 参数 | 飞鼠档位→增益 | `0.2 + level × 0.18` | `main.cpp:48` |
| 参数 | 触控档位→增益 | `0.5 + level × 0.25` | `main.cpp:49` |
| 参数 | NVS 命名空间 | `mouse_cfg` | `main.cpp:57` |

> 已移除（v3.1 随 EC11 清理）：`KNOB_DEBOUNCE_MS = 120ms`、`PIN_M2_TIM_CH1/CH2`。


# display_mode

# 可穿戴智能飞鼠/触控设备 OLED 显示系统技术开发笔记

---

## 目录
1. [模块概述与文件职责](#一-模块概述与文件职责)
2. [底层显示机制与显存双缓冲](#二-底层显示机制与显存双缓冲)
3. [模式一：空中飞鼠与滚轮断控界面 (`updateDisplayMode1`)](#三-模式一空中飞鼠与滚轮断控界面-updatedisplaymode1)
4. [模式二：触控板与多点交互界面 (`updateDisplayMode2`)](#四-模式二触控板与多点交互界面-updatedisplaymode2)
5. [独立滚轮配置界面 (`drawWheelConfigUI`)](#五-独立滚轮配置界面-drawwheelconfigui)
6. [嵌入式 C/C++ 核心语法与设计技巧](#六-嵌入式-cc-核心语法与设计技巧)
7. [Adafruit_GFX & SSD1306 常用 API 查阅表](#七-adafruit_gfx--ssd1306-常用-api-查阅表)

---

## 一、 模块概述与文件职责

本模块是可穿戴智能交互设备（集成空中飞鼠与电容触摸板）的人机交互（HMI）显示核心，基于 I2C 接口的 0.96 寸 128×64 单色 OLED（驱动芯片为 SSD1306）。

* **`display_mode.h`**：
  * 对外导出全局唯一的显示屏驱动实例 `extern Adafruit_SSD1306 display`。
  * 声明系统的初始化与各工作模式的画面刷新接口，供主调度循环及事件任务调用。
* **`display_mode.cpp`**：
  * 实现 SSD1306 对象的具体实例化与 I2C 总线初始化。
  * 实现不同业务模式下的 UI 布局排版、数学坐标映射、动态图形绘制与显存提交。

---

## 二、 底层显示机制与显存双缓冲

### 1. 显存结构计算
SSD1306 控制器的屏幕物理分辨率为 $128 \times 64$ 像素。作为单色点阵屏，每个像素点仅需要 1 bit 表示开关状态（`1` 为点亮，`0` 为熄灭）：

$$\text{显存总大小} = \frac{128 \times 64 \text{ bits}}{8 \text{ bits/Byte}} = 1024 \text{ Bytes} = 1 \text{ KB}$$

这 1024 字节在 MCU（ESP32/STM32）的 SRAM 中分配为一个离屏缓冲区（Off-screen Framebuffer）。

### 2. 双缓冲与单帧渲染生命周期
由于 I2C 总线（标准速率 100 kHz 或快速模式 400 kHz）数据传输速率有限，如果直接对屏幕硬件逐点绘制，会造成严重的屏幕撕裂与文字残影。代码采用标准的**显存双缓冲机制**：

```
       [ 1. 帧起始：显存清零 ]
        display.clearDisplay();
                  │
                  ▼
       [ 2. 离屏渲染：写入 MCU SRAM ]
        绘制矩形、线段、文字、位图、圆点...
        (全部在 1KB 内存数组中进行位操作)
                  │
                  ▼
       [ 3. 帧提交：DMA / I2C 硬件突发传输 ]
        display.display();
```

* **`display.clearDisplay()`**：将 MCU 内部的 1024 字节缓冲区全部写 0。
* **图形与文字 API**：计算像素在缓冲区中的字节偏移 `(y / 8) * 128 + x` 与位偏移 `y % 8`，并修改该 bit。
* **`display.display()`**：将整个 1024 字节缓冲区通过 I2C 总线连续推送到 OLED 控制器的内部 GDDRAM 中，瞬间完成物理屏幕的整体更新。

---

## 三、 模式一：空中飞鼠与滚轮断控界面 (`updateDisplayMode1`)

### 1. 业务逻辑与全局依赖
该函数用于呈现 MPU6050 运动解算后的鼠标运行状态。通过外部状态变量 `is_airmouse_locked` 分割为两个子状态：

| 运行状态 | `is_airmouse_locked` | 顶栏标题 | 核心显示内容 | 右侧动效部件 |
| :--- | :--- | :--- | :--- | :--- |
| **正常飞鼠** | `false` | `M1: AIR MOUSE` | 鼠标相对位移 $dX/dY$、鼠标灵敏度 | 十字雷达瞄准准星 + 动态跟踪点 |
| **断控/滚轮**| `true` | `M1: PAUSE & SCROLL` | 滚轮状态提示、滚轮灵敏度 | 模拟滚轮外框 + 实时滚动方向三角形 |

#### 跨模块参数定义（`extern` 声明）：
* `global_sens_percent`：飞鼠指针移动的缩放百分比（1% ~ 100%）。
* `global_wheel_sens_percent`：滚轮单次步进位移百分比（1% ~ 100%）。
* `is_airmouse_locked`：断控切换标志位。
* `g_scroll_dir`：滚轮实时方向（`+1` 向上滚，`-1` 向下滚，`0` 静止）。

---

### 2. 核心视觉组件与动态映射算法

#### (1) 反显状态栏（Inverted Header）
```cpp
display.fillRect(0, 0, 128, 12, SSD1306_WHITE);
display.setTextColor(SSD1306_BLACK);
display.setCursor(4, 2);
display.print(is_airmouse_locked ? "M1: PAUSE & SCROLL" : "M1: AIR MOUSE");
display.setCursor(95, 2);
display.print(is_connected ? "CONN" : "DISC");
```
* **实现原理**：在 $0 \le Y < 12$ 的高度内绘制一个全白矩形，随后将文本前景色设置为 `SSD1306_BLACK`。黑字白底能与下方的暗色工作区形成明显的视觉区隔。

#### (2) 动态准星雷达图（Radar Crosshair）
在正常飞鼠状态下，屏幕右侧 $(104, 33)$ 处生成一个微型准星：
* 外圆环：半径为 8 px 的空心圆。
* 十字线：长 24 px 的水平线与垂直线，正中心穿过圆心。
* **位置约束与映射（Clamping）**：
  ```cpp
  int16_t offset_x = constrain(x, -6, 6);
  int16_t offset_y = constrain(y, -6, 6);
  display.fillCircle(centerX + offset_x, centerY + offset_y, 2, SSD1306_WHITE);
  ```
  * **数学意义**：鼠标高速甩动时，$dX, dY$ 可能会达到几十甚至上百。为防止中心指示点脱离雷达外框，通过 `constrain()` 函数将位移严格钳位在 $[-6, +6]$ 像素闭区间内，确保指示点始终在半径为 8 的雷达圆内受限运动。

#### (3) 滚轮滚动动态动画
在断控滚轮状态下，右侧 $(108, 33)$ 绘制带有圆角的模拟滚轮外框（长宽 $12 \times 24$ px）：
* **方向判定渲染**：
  * `g_scroll_dir > 0`（向上滚动）：绘制实心向上三角形顶点在 $(108, 25)$。
  * `g_scroll_dir < 0`（向下滚动）：绘制实心向下三角形顶点在 $(108, 41)$。
  * `g_scroll_dir == 0`（静止无操作）：在圆心绘制半径为 2 px 的微小实心圆点。

#### (4) 底部自适应灵敏度进度条
位于屏幕底部（$X=4, Y=52, W=120, H=6$）：
```cpp
uint8_t current_render_percent = is_airmouse_locked ? global_wheel_sens_percent : global_sens_percent;
int fill_w = (int)((float)current_render_percent / 100.0f * (bar_w - 4));
display.fillRect(bar_x + 2, bar_y + 2, fill_w, bar_h - 4, SSD1306_WHITE);
```
* **线性变换公式**：
  $$\text{fill\_w} = \left\lfloor \frac{P}{100.0} \times (W_{\text{bar}} - 4) \right\rfloor$$
  其中 $P \in [1, 100]$，可填充最大宽度为 $120 - 4 = 116\text{ px}$。

---

## 四、 模式二：触控板与多点交互界面 (`updateDisplayMode2`)

### 1. 业务逻辑与全局依赖
该函数主要对接电容式触摸屏（如 FT6336U）。根据检测到的物理触点数量 `oled_touch_points` 分为两类视觉布局：

| 触摸状态 | `oled_touch_points` | 顶栏标题 | 数据显示区 |
| :--- | :--- | :--- | :--- |
| **单指触控** | `1` | `M2: TOUCH PAD` | 单点坐标 $(X, Y)$、连续点击次数队列 |
| **双指触控** | `2` | `M2: DUAL-TOUCH`| 点1坐标、点2坐标、两点欧氏间距 |

#### 跨模块参数定义（`extern` 声明）：
* `oled_touch2_x`, `oled_touch2_y`：第二个触点的绝对物理坐标。
* `oled_touch_points`：当前有效触点数（1 或 2）。
* `oled_click_count`：当前连击计数值（0 ~ 5）。

---

### 2. 核心数学计算与图形渲染

#### (1) 双触点空间欧几里得距离计算
```cpp
int16_t dist = sqrt(pow(x - oled_touch2_x, 2) + pow(y - oled_touch2_y, 2));
display.printf("Dist: %d px", dist);
```
* **原理**：利用勾股定理计算两个触点的直线像素距离：
  $$d = \sqrt{(x_1 - x_2)^2 + (y_1 - y_2)^2}$$
  在双指手势算法中，该数值的实时变化率是判定 Pinch-to-Zoom（双指缩放）的核心输入量。

#### (2) 点击队列图标绘制
```cpp
display.print("Click Que: ");
for(int i = 0; i < oled_click_count; i++) {
    display.fillCircle(68 + (i * 10), 35, 3, SSD1306_WHITE);
}
if(oled_click_count == 0) display.print("none");
```
* **逻辑**：采用计数循环将点击事件以排队点的方式沿 X 轴水平分布（步进 10 px），直观呈现状态机捕获的双击或多击动作。

#### (3) 虚拟触摸视口与相对映射
在屏幕右侧开辟了一个微缩的虚拟触摸板视口矩形（$X=92, Y=16, W=34, H=44$）：
* **虚线网格绘制**：
  ```cpp
  for(int i = box_x + 2; i < box_x + box_w; i += 4) display.drawPixel(i, box_y + (box_h / 2), SSD1306_WHITE);
  for(int j = box_y + 2; j < box_y + box_h; j += 4) display.drawPixel(box_x + (box_w / 2), j, SSD1306_WHITE);
  ```
  以步进 4 像素画点，形成水平和垂直的中心参考虚线十字网格。
* **触点微缩映射（取模限制）**：
  ```cpp
  int m1_x = box_x + 2 + (abs(x) % (box_w - 4));
  int m1_y = box_y + 2 + (abs(y) % (box_h - 4));
  ```
  * **原理**：触摸屏的物理坐标（如 $0 \sim 320$）远大于虚拟视口尺寸（有效绘制区域仅 $30 \times 40$ px）。通过 `abs(coord) % (bound - 4)` 快速将坐标折叠在视口边界内，利用 5 像素的十字光标指示指尖触点。
* **双指中点（Centroid）计算**：
  ```cpp
  display.drawPixel((m1_x + m2_x) / 2, (m1_y + m2_y) / 2, SSD1306_WHITE);
  ```
  在两个触点的几何中心点亮单像素点，可用于辅助观察手势中心旋转或平移基准。

---

## 五、 独立滚轮配置界面 (`drawWheelConfigUI`)

该函数是一套独立的滚轮参数调测界面，用于展示浮点型灵敏度倍率与滚动状态指示。

### 1. 浮点灵敏度向进度条映射
函数接收 `float wheel_sens`（典型输入范围为 $1.0 \sim 4.0$）：
```cpp
float percent = (wheel_sens - 1.0f) / 3.0f;
int fill_w = (int)(percent * (bar_w - 4));
if (fill_w < 1 && wheel_sens > 0) fill_w = 4;
```
* **归一化算法**：
  $$\text{percent} = \frac{\text{wheel\_sens} - 1.0}{4.0 - 1.0} = \frac{\text{wheel\_sens} - 1.0}{3.0}$$
* **边界防御保护**：当 $\text{wheel\_sens} > 0$ 但计算出的宽度小于 1 px 时，强制赋予 4 px 的最小宽度保底，避免用户在低灵敏度下看到空进度条而误以为设备断电或参数清零。

---

## 六、 嵌入式 C/C++ 核心语法与设计技巧

### 1. `extern` 关键字与多文件链接
* **语法含义**：在 `.cpp` 文件顶部书写 `extern uint8_t global_sens_percent;`，告知编译器该变量在外部其他目标文件（如 `mouse_mode.cpp`）中已完成内存分配，当前文件仅生成未解析的符号引用，由链接器（Linker）统一做符号地址绑定。
* **工程优势**：确保全局状态唯一性，避免在多个源文件中重复定义导致的 `multiple definition of ...` 错误。

### 2. 格式化输出对齐防抖动：`printf` 左对齐占位符
```cpp
display.printf("dX: %-4d  dY: %-4d", x, y);
display.printf("P1 X:%-3d Y:%-3d", x, y);
```
* **占位符解析**：
  * `%d`：有符号十进制整数输出。
  * `4`：字段最小宽度为 4 个字符。
  * `-`：**强制左对齐**（默认右对齐）。
* **UI 防抖设计考量**：在 OLED 上绘制字符时，如果数值从 `-12` 突变到 `5`，若不固定字符宽度，文本总长度会改变，导致后面的标签发生水平抖动，甚至在无背景擦除模式下留下上一帧的字符残影。左对齐固定占位符能够保证数据展示的几何对齐稳定性。

### 3. 数值限幅：`constrain(amt, low, high)`
```cpp
int16_t offset_x = constrain(x, -6, 6);
```
* Arduino 核心标准宏函数，等价于：
  $$\text{result} = \begin{cases} low & \text{if } amt < low \\ high & \text{if } amt > high \\ amt & \text{otherwise} \end{cases}$$
* 常用于嵌入式图形界面中防止光标脱出视口外边框，引发数组越界或破坏其他区域显存。

### 4. 头文件包含防卫宏（Include Guard）
```cpp
#ifndef DISPLAY_MODE_H
#define DISPLAY_MODE_H
// ... 声明内容 ...
#endif // DISPLAY_MODE_H
```
* 避免同一个头文件在复杂的工程依赖中被多次包含，消除预处理阶段结构体、类或函数声明的重复定义冲突。

---

## 七、 Adafruit_GFX & SSD1306 常用 API 查阅表

| 函数原型 | 核心功能说明 | 渲染注意事项 / 性能建议 |
| :--- | :--- | :--- |
| `begin(vcc_state, i2c_addr)` | 初始化 SSD1306 硬件，配置电荷泵与 I2C 通信地址（默认 `0x3C`）。 | 若返回 `false` 说明 I2C 接线异常或地址错误，需卡住并排查。 |
| `clearDisplay()` | 将 MCU 本地显存缓冲区全部写 0。 | **每帧绘制前必须调用**，否则新画面会与上一帧重叠。 |
| `display()` | 将 1KB 本地缓冲区通过 I2C 整体刷新到屏幕硬件。 | **每帧最后调用一次**。切忌在循环或每个图形后调用，否则大幅降低帧率。 |
| `setTextWrap(bool)` | 设置文本遇到右边界是否自动换行。 | 设为 `false` 可防止动态数据过长时换行覆盖下一行界面。 |
| `setCursor(x, y)` | 移动文本输出起始光标坐标（像素单位）。 | 字体基线以此为基础排版，默认字高通常为 8 px。 |
| `setTextColor(c)` | 设置文本前景色（`SSD1306_WHITE` 或 `SSD1306_BLACK`）。 | 配合 `fillRect` 背景可快速实现反显高亮状态栏。 |
| `drawFastHLine(x, y, w, c)`| 快速水平线绘制。 | 算法进行了按字节对齐优化，**执行速度显著快于 `drawLine`**。 |
| `drawFastVLine(x, y, h, c)`| 快速垂直线绘制。 | 算法内部针对位操作优化，绘制纵向分割线首选。 |
| `drawRect(x, y, w, h, c)`  | 绘制矩形空心线框。 | 适用于外视口、进度条槽等边界容器。 |
| `fillRect(x, y, w, h, c)`  | 绘制实心填充矩形。 | 常用于反显背景擦除、进度条填充。 |
| `drawCircle(x, y, r, c)`   | 绘制空心圆（Bresenham 算法）。 | 常用于瞄准环、静态指示圆环。 |
| `fillCircle(x, y, r, c)`   | 绘制实心圆。 | 常用于光标点、队列状态指示点。 |
| `fillTriangle(...)`        | 绘制实心三角形。 | 传入三个顶点坐标，常用于滚动方向、展开折叠等指示箭头。 |
| `drawPixel(x, y, c)`       | 控制单像素点亮/熄灭。 | 性能开销较低，适合通过循环绘制网格虚线或散点图。 |



# IMU 姿态解算模块学习笔记 (`imu_processing`)

## 1. 模块定位与架构
本模块通常位于嵌入式传感器驱动层之上、应用业务层（如空中飞鼠、航模控制）之下，负责将 MPU6050 采集的原始数据转化为三维空间欧拉角（Euler Angles）。

```
+------------------------------------+
|  应用业务层 (如 mouse_mode.c)       |
+-----------------+------------------+
                  |  调用 API (读欧拉角/复位)
                  v
+-----------------+------------------+
|  姿态解算层 (imu_processing.c/.h)  | <--- [当前模块]
+-----------------+------------------+
                  |  获取 Raw Data
                  v
+-----------------+------------------+
|  底层驱动层 (mpu6050_driver.c/.h)  |
+------------------------------------+
```

---

## 2. 核心数据结构与宏定义

### 2.1 欧拉角结构体 (`euler_angles_t`)
```c
typedef struct {
    float pitch; // 俯仰角 (绕 Y 轴旋转)
    float roll;  // 翻滚角 (绕 X 轴旋转)
    float yaw;   // 偏航角 / 航向角 (绕 Z 轴旋转)
} euler_angles_t;
```

### 2.2 传感器转换系数与滤波参数
* `GYRO_SCALE (65.5f)`：MPU6050 在 $\pm 500^\circ/\text{s}$ 量程下的灵敏度系数。将原始 ADC 整数除以此值可转换为度每秒 ($^\circ/\text{s}$)。
* `ACCEL_SCALE (16384.0f)`：MPU6050 在 $\pm 2g$ 量程下的灵敏度系数。将原始加速度除以此值转换为重力加速度单位 $g$。
* `ALPHA (0.98f)`：互补滤波加权系数。
  $$\text{Angle} = \alpha \cdot (\text{Angle} + \text{Gyro} \cdot \Delta t) + (1 - \alpha) \cdot \text{AccAngle}$$
  * 高频信任陀螺仪积分（动态快，无加速度振动干扰，但会漂移）。
  * 低频信任加速度计倾角（静态准，依靠重力矢量校正漂移）。

---

## 3. 函数接口与职责

| 函数原型 | 作用 | 典型触发场景 |
| :--- | :--- | :--- |
| `void imu_calibrate(void)` | 零漂校准与状态清零 | 系统上电启动、设备静止放置时 |
| `euler_angles_t imu_update_angles(void *raw, float dt)` | 姿态解算主步进驱动 | 定时器中断或主循环（100Hz~500Hz 周期更新） |
| `void imu_reset_orientation(void)` | 偏航角归零 (`current_yaw = 0`) | 用户按下飞鼠“对中/重新标定”实体按键时 |
| `euler_angles_t imu_get_current_angles(void)` | 无副作用获取当前角度 | UI 渲染、按键轮询等其他非解算任务读取状态时 |

---

## 4. 关键设计亮点

1. **信息隐藏与模块封装**
   * 全局状态变量（`gyro_offset_*`、`current_*`）全部采用 `static` 修饰，避免全局命名污染，仅通过显式函数对外交互。
2. **C / C++ 混编兼容**
   * 头文件包含 `#ifdef __cplusplus extern "C" { #endif` 块，确保在 C++（如 Arduino 框架）下调用时符号不被修饰（Name Mangling）。
3. **防御性编程**
   * 对常数 `PI` 采用了 `#ifndef PI` 包裹，避免不同第三方库或 SDK 重复宏定义导致的编译警告/报错。

---

## 5. 后续完善方向（待实现项）

- [ ] **完善校准算法**：在 `imu_calibrate()` 中实现静止多次（如 200 次）采样求均值，准确获取三轴零漂 offset。
- [ ] **补全互补滤波解算**：在 `imu_update_angles()` 中将 `raw_data` 强转为具体加速度/陀螺仪结构体，完成重力倾角换算及互补滤波计算。
- [ ] **漂移与万向锁处理**：当前 6 轴 IMU（无磁力计）Yaw 轴无绝对地磁参考，长时运行易漂移；若角度倾斜过大，后续可升级为四元数（Quaternion）更新以规避欧拉角奇异点问题。



# ESP32 BLE 空中飞鼠系统源码全景深度学习笔记 (`mouse_mode`)

---

## 1. 模块架构与数据流总览

本模块实现了一个兼具**空中飞鼠光标**与**虚拟平滑滚轮**的复合输入设备，通过宏观状态机标志位 `is_airmouse_locked` 划分了两种完全独立的控制拓扑：

```
                           +------------------------+
                           |    updateMouseMode()   | <--- 主循环轮询 (高频调用)
                           +-----------+------------+
                                       |
    +----------------------------------+----------------------------------+
    |                                  |                                  |
    v                                  v                                  v
+-----------------------+  +-----------------------+  +-----------------------+
|  updateLockKeyLogic() |  |  updateSensKeyLogic() |  | updateNormalButtons() |
+-----------------------+  +-----------------------+  +-----------------------+
| 检测 PIN_M1_PAUSE     |  | 检测 PIN_M1_SENS      |  | 检测左右键并根据状态机分流:
| 翻转 is_airmouse_locked|  | 短按 +10% 循环步进    |  | [未锁定] -> 左/右键标准点击
| 清空滤波残余与滚轮缓存|  | 长按(>400ms)自动连调  |  | [锁定]   -> 虚拟动力学滚轮
+-----------------------+  +-----------------------+  +-----------------------+
                                       |
                                       v
                    +------------------------------------+
                    |  !is_airmouse_locked 光标计算流程  |
                    +------------------------------------+
                    | 1. 12ms (约83Hz) 定时采样          |
                    | 2. 扣除陀螺仪零漂校准值            |
                    | 3. 原始层动态死区门限过滤          |
                    | 4. DAMPING 阻尼衰减转换            |
                    | 5. 一阶低通平滑滤波 (ALPHA=0.4)    |
                    | 6. air_gain 增益放大               |
                    | 7. 像素死区二次截断                |
                    | 8. 发送 BleMouseDevice.move(x,y,0) |
                    +------------------------------------+
```

---

## 2. 全局与静态状态变量深度剖析

```c
extern BleMouse BleMouseDevice;
extern uint8_t global_sens_percent;
extern uint8_t global_wheel_sens_percent;
extern SystemConfig g_cfg;
```
* **`BleMouse BleMouseDevice`**：由外部实例化（通常在主程序或蓝牙初始化文件）的 ESP32 BLE HID 鼠标对象，提供标准的 `press`、`release`、`move` 等蓝牙通信方法。
* **`global_sens_percent / global_wheel_sens_percent`**：分别控制飞鼠光标与滚轮灵敏度的百分比系数（有效取值 $10 \sim 100$），步长为 $10$。
* **`g_cfg`**：系统持久化配置结构体，内含 `air_gain`（增益放大倍数）、`deadzone_px`（像素死区）、`air_dpi_level` 及 `touch_dpi_level` 等字段。

```c
int16_t oled_mouse_x = 0;
int16_t oled_mouse_y = 0;
bool is_airmouse_locked = false;
volatile int32_t mouseWheelCount = 0;
int8_t g_scroll_dir = 0;
```
* **`oled_mouse_x / oled_mouse_y`**：记录当前帧经过处理后的位移量，暴露给外部 OLED 驱动用于在屏幕上绘制准心或十字光标微缩轨迹。
* **`is_airmouse_locked`**：状态机切换标志。`false` 为常规飞鼠；`true` 为滚轮锁定模式。
* **`mouseWheelCount`**：滚轮累计滚动格数。声明为 `volatile` 防止多任务或中断上下文优化，供 UI 实时展示滚动量。
* **`g_scroll_dir`**：滚轮物理瞬时方向（`+1`：上滚，`-1`：下滚，`0`：静止）。

```c
int32_t gyro_offset_pitch = 0;
int32_t gyro_offset_yaw = 0;

const int16_t DAMPING = 120;
static float smooth_dx = 0;
static float smooth_dy = 0;
const float FILTER_ALPHA = 0.4f;
static unsigned long lastBleTime = 0;

static float scroll_accumulator_up = 0.0f;
static float scroll_accumulator_down = 0.0f;
static unsigned long lastWheelTickTime = 0;
```
* **`gyro_offset_*`**：上电校准时累加求均值得到的静止零偏（静态误差），防止静止时鼠标自动飘移。
* **`DAMPING (120)`**：将传感器高频 ADC 读数换算为基础位移的降速阻尼系数。
* **`smooth_dx / smooth_dy`**：一阶低通滤波器的历史状态保持器。
* **`FILTER_ALPHA (0.4f)`**：低通平滑权重系数。
* **`scroll_accumulator_*`**：由于 HID 滚轮数据包只接收整型（`int8_t`，一次至少滚 1 格），当灵敏度较低时必须将每次计算的小数增量暂存在浮点累加器中，直到累加满 1.0 格才发送。

---

## 3. 函数级全流程深度逐行解析

### 3.1 `initMouseMode(void)` —— 硬件与参数初始化

#### 源码实现与逐行拆解：
```c
void initMouseMode() {
    is_airmouse_locked = false;   // 1. 初始化状态：默认启用飞鼠光标，不锁定
    mouseWheelCount = 0;          // 2. 清零滚轮总步数统计
    scroll_accumulator_up = 0.0f; // 3. 清空向上滚轮小数累加器
    scroll_accumulator_down = 0.0f;// 4. 清空向下滚轮小数累加器
    smooth_dx = 0;                // 5. 清除 X 轴低通滤波历史残余
    smooth_dy = 0;                // 6. 清除 Y 轴低通滤波历史残余
    
    // 7. 配置触摸按键引脚：由于外部触摸芯片通常高电平有效，设置为浮空输入模式
    pinMode(PIN_M1_LEFT, INPUT);    // 左键 (GPIO)
    pinMode(PIN_M1_RIGHT, INPUT);   // 右键 (GPIO)
    pinMode(PIN_M1_PAUSE, INPUT);   // 锁定/暂停切换键 (GPIO 2)
    pinMode(PIN_M1_SENS, INPUT);    // 灵敏度步进键 (GPIO 3)

    // 8. 底层 MPU6050 初始化并等待总线稳定
    mpu6050_init();
    delay(50); 

    // 9. 零漂静态标定（50次均值滤波）
    gyro_offset_pitch = 0; 
    gyro_offset_yaw = 0;
    for (int i = 0; i < 50; i++) {
        mpu6050_raw_data_t temp_data;
        mpu6050_read_raw(&temp_data); 
        
        // 关键内存指针技巧：mpu6050_raw_data_t 内部是 7 个 int16_t（Accel 3轴, Temp 1个, Gyro 3轴）
        // ptr[0]=Accel_X, ptr[1]=Accel_Y, ptr[2]=Accel_Z, ptr[3]=Temp, ptr[4]=Gyro_X, ptr[5]=Gyro_Y, ptr[6]=Gyro_Z
        int16_t *ptr = (int16_t *)&temp_data; 
        
        // 此处飞鼠选取 ptr[5](Gyro_Y，即 Pitch 轴) 和 ptr[6](Gyro_Z，即 Yaw 轴) 进行累加
        gyro_offset_pitch += ptr[5]; 
        gyro_offset_yaw += ptr[6];
        delay(5); // 每次采样间隔 5ms，总校准耗时 250ms
    }
    
    // 10. 计算 50 次静态采样的算术平均值作为系统基准零偏
    gyro_offset_pitch /= 50; 
    gyro_offset_yaw /= 50;
    Serial.println("[M1] 触摸按键版飞鼠模式初始化完成。");
}
```

#### 函数设计亮点：
* **结构体指针转换技巧 (`ptr[5] / ptr[6]`)**：通过将原始结构体强转为连续的 `int16_t*` 数组，避免了逐个引用具名成员的繁琐写法，直接提取角速度原始数据。
* **物理轴映射对应**：
  * **手腕左右晃动**：绕垂直 Z 轴旋转（Yaw，对应数据索引 `6`），映射到屏幕 **X 轴位移**。
  * **手腕上下抬起**：绕横向 Y 轴旋转（Pitch，对应数据索引 `5`），映射到屏幕 **Y 轴位移**。

---

### 3.2 `updateLockKeyLogic(void)` —— 模式切换与状态清道夫

#### 源码实现与逐行拆解：
```c
static void updateLockKeyLogic() {
    static bool lastLockBtnState = TOUCH_RELEASED;
    static unsigned long lockBtnDebounceTime = 0;
    bool currentLockState = digitalRead(PIN_M1_PAUSE); // 读取锁定按键电平
    
    // 边沿变化判定
    if (currentLockState != lastLockBtnState) {
        // 软件去抖动：触发时间间隔必须大于 50ms
        if (millis() - lockBtnDebounceTime > 50) {
            lockBtnDebounceTime = millis();
            
            // 仅在上升沿（TOUCH_PRESSED，即手指刚按上按键的一瞬间）执行模式反转
            if (currentLockState == TOUCH_PRESSED) {
                is_airmouse_locked = !is_airmouse_locked; // 模式翻转核心
                
                // 状态环境清理（极为关键的工业级防粘连机制）：
                g_scroll_dir = 0;
                mouseWheelCount = 0;
                scroll_accumulator_up = 0.0f;
                scroll_accumulator_down = 0.0f;
                smooth_dx = 0; // 清除飞鼠平滑器历史累加，避免切回光标时产生飞窜跳跃
                smooth_dy = 0;

                // 强制向主机发送按键弹起，防止切模式瞬间由于漏掉 Release 事件导致鼠标在系统层面“卡死按住”
                BleMouseDevice.release(MOUSE_LEFT);
                BleMouseDevice.release(MOUSE_RIGHT);

                Serial.printf("🖱️ [M1] 状态切换 -> 滚轮锁定模式: %s\r\n", is_airmouse_locked ? "开启" : "关闭");
            }
            lastLockBtnState = currentLockState; // 同步状态
        }
    }
}
```

#### 函数核心机制：
* **模式切换时的状态隔离**：进入滚轮模式瞬间，不仅关停光标更新，还同步将 `smooth_dx/dy` 与 HID 按钮全部释放。如果用户按着左键拖拽文件时突然按暂停，该逻辑会切断长按状态，防止操作系统出现“粘滞键”逻辑 bug。

---

### 3.3 `updateSensKeyLogic(void)` —— 复合按键交互（短按调节 + 长按步进）

#### 源码实现与逐行拆解：
```c
static void updateSensKeyLogic() {
    static bool lastSensBtnState = TOUCH_RELEASED;
    static unsigned long sensBtnPressTime = 0;
    static unsigned long lastAutoStepTime = 0;
    static bool isLongPressed = false;
    bool currentSensState = digitalRead(PIN_M1_SENS);

    // 1. 上升沿：手指触碰按键瞬间
    if (currentSensState == TOUCH_PRESSED && lastSensBtnState == TOUCH_RELEASED) {
        sensBtnPressTime = millis(); // 记录触摸开始时刻
        isLongPressed = false;       // 复位长按标志
        delay(5);                    // 微小延时避开接触瞬间电平毛刺
    }
    
    // 2. 持续按住状态检测（长按自动连击动力学）
    if (currentSensState == TOUCH_PRESSED) {
        // 如果持续触摸时间超过 400ms，判定进入长按连调机制
        if (millis() - sensBtnPressTime >= 400) {
            isLongPressed = true;
            // 连调频率限制：每 150ms 自动步进一次
            if (millis() - lastAutoStepTime >= 150) {
                lastAutoStepTime = millis();
                
                // 根据当前所属模式分别调整对应的参数
                if (!is_airmouse_locked) {
                    global_sens_percent += 10;
                    if (global_sens_percent > 100) global_sens_percent = 10; // 达到 100% 自动回滚到 10%
                    g_cfg.air_dpi_level = global_sens_percent / 10;          // 映射为 1~10 档位
                } else {
                    global_wheel_sens_percent += 10;
                    if (global_wheel_sens_percent > 100) global_wheel_sens_percent = 10;
                    g_cfg.touch_dpi_level = global_wheel_sens_percent / 10;
                }
                saveConfigToNVS(); // 每次连击步进都落盘到 ESP32 NVS 闪存
            }
        }
    }
    
    // 3. 下降沿：手指离开触摸电极瞬间（短按单次触发）
    if (currentSensState == TOUCH_RELEASED && lastSensBtnState == TOUCH_PRESSED) {
        // 判定条件：没有触发过长按模式，且按压有效时间大于 25ms（滤除静电杂波脉冲）
        if (!isLongPressed && (millis() - sensBtnPressTime > 25)) {
            if (!is_airmouse_locked) {
                global_sens_percent += 10;
                if (global_sens_percent > 100) global_sens_percent = 10;
                g_cfg.air_dpi_level = global_sens_percent / 10;
            } else {
                global_wheel_sens_percent += 10;
                if (global_wheel_sens_percent > 100) global_wheel_sens_percent = 10;
                g_cfg.touch_dpi_level = global_wheel_sens_percent / 10;
            }
            saveConfigToNVS(); // 单击落盘
        }
    }
    lastSensBtnState = currentSensState; // 状态迭代
}
```

#### 关键时序分析：
* **防冲突设计**：通过 `isLongPressed` 标志锁。如果在长按过程中已经触发了多次自动连击，手指抬起时下降沿逻辑会被屏蔽，绝不会在连调结束时多跳一次步进。

---

### 3.4 `updateNormalButtons(void)` —— 按键复用与滚轮平滑引擎

这是整个代码中分支最复杂的核心逻辑，根据 `is_airmouse_locked` 的状态决定是输出鼠标按键还是虚拟滚轮。

#### 3.4.1 分支 A：正常模式（鼠标点击映射）
```c
if (!is_airmouse_locked) {
    scroll_accumulator_up = 0.0f;   // 处于非锁定模式，滚轮累加器时刻清零
    scroll_accumulator_down = 0.0f;

    // 左键 20ms 去抖与按下/释放事件
    if (currLeft != lastLeftState) {
        if (now - lastLeftDebounce > 20) {
            lastLeftDebounce = now;
            if (currLeft == TOUCH_PRESSED) {
                BleMouseDevice.press(MOUSE_LEFT);   // 发送 HID Mouse Report：左键置 1
            } else {
                BleMouseDevice.release(MOUSE_LEFT); // 发送 HID Mouse Report：左键置 0
            }
            lastLeftState = currLeft;
        }
    }

    // 右键 20ms 去抖与按下/释放事件
    if (currRight != lastRightState) {
        if (now - lastRightDebounce > 20) {
            lastRightDebounce = now;
            if (currRight == TOUCH_PRESSED) {
                BleMouseDevice.press(MOUSE_RIGHT);  // 发送 HID Mouse Report：右键置 1
            } else {
                BleMouseDevice.release(MOUSE_RIGHT);// 发送 HID Mouse Report：右键置 0
            }
            lastRightState = currRight;
        }
    }
}
```

#### 3.4.2 分支 B：滚轮锁定模式（虚拟动力学滚轮）
在此模式下，左右键被重映射为滚轮的“向上”和“向下”。

##### 第一步：触摸瞬发（首包零延迟响应）
```c
// 左键（向上）边沿检测
if (currLeft != lastLeftState) {
    if (now - lastLeftDebounce > 20) {
        lastLeftDebounce = now;
        lastLeftState = currLeft;
        if (currLeft == TOUCH_PRESSED) {
            // 🚀 核心触感优化：手指刚碰上去的一瞬间，不走动力学累加，直接强发 1 格滚轮
            BleMouseDevice.move(0, 0, 1);
            g_scroll_dir = 1;
            mouseWheelCount += 1;
            scroll_accumulator_up = 0.0f; // 重置累加器，准备进入持续长按
        } else {
            // 🛑 瞬时刹车：手指松开电极瞬间，物理状态立刻置零，停止一切滚动
            g_scroll_dir = 0;
            scroll_accumulator_up = 0.0f;
        }
    }
}
// （右键向下检测逻辑同理，发送 BleMouseDevice.move(0, 0, -1)）
```

##### 第二步：长按动力学累加器（20ms 周期连发引擎）
```c
if (now - lastWheelTickTime >= 20) {
    lastWheelTickTime = now;

    // 动力学数学映射公式：
    // speed 取值范围为 [0.05, 0.85]
    // 对应灵敏度从 10% 到 100%
    float speed = 0.05f + ((float)global_wheel_sens_percent / 100.0f) * 0.80f;

    // 左键长按：持续向上累加
    if (currLeft == TOUCH_PRESSED && lastLeftState == TOUCH_PRESSED) {
        scroll_accumulator_up += speed; // 累加小数增量
        
        // 当累加值超过 1.0 时触发实际 HID 滚轮数据包
        if (scroll_accumulator_up >= 1.0f) {
            int8_t step = (int8_t)scroll_accumulator_up; // 取整数部分（例如 1.25 -> 1）
            scroll_accumulator_up -= step;               // 扣除整数，保留小数余量 (0.25)
            BleMouseDevice.move(0, 0, step);             // 发送滚轮报告
            mouseWheelCount += step;
        }
    }

    // 右键长按：持续向下累加
    if (currRight == TOUCH_PRESSED && lastRightState == TOUCH_PRESSED) {
        scroll_accumulator_down += speed;
        if (scroll_accumulator_down >= 1.0f) {
            int8_t step = (int8_t)scroll_accumulator_down;
            scroll_accumulator_down -= step;
            BleMouseDevice.move(0, 0, -step);            // 发送滚轮向下步进
            mouseWheelCount -= step;
        }
    }
}
```

---

### 3.5 `updateMouseMode(void)` —— 主任务执行与 IMU 滤波核心算法

#### 源码实现与算法拆解：
```c
void updateMouseMode() {
    // 1. 连接状态卫语句：若蓝牙未与电脑/手机建立连接，则不消耗算力计算直接返回
    if (!BleMouseDevice.isConnected()) return;

    // 2. 依次轮询三个按键子状态机
    updateLockKeyLogic();
    updateSensKeyLogic();
    updateNormalButtons();

    // 3. 飞鼠光标核心处理流（仅在未锁定模式运行）
    if (!is_airmouse_locked) {
        unsigned long now = millis();
        // 频率控制器：限制在 12ms 执行一次（约 83.3Hz 采样率）
        if (now - lastBleTime >= 12) {
            lastBleTime = now;
            
            // A. 读取 MPU6050 原始 ADC 数据
            mpu6050_raw_data_t my_mpu_data;
            mpu6050_read_raw(&my_mpu_data);
            int16_t *data_ptr = (int16_t *)&my_mpu_data;
            
            // B. 零漂消除 (校准补偿)
            int16_t cal_pitch = data_ptr[5] - gyro_offset_pitch;
            int16_t cal_yaw   = data_ptr[6] - gyro_offset_yaw;
            
            // C. 动态原始死区过滤 (Dynamic Deadzone)
            // 根据配置的死区放大 40 倍与原始传感器值比对
            int16_t dynamic_deadzone = (int16_t)(g_cfg.deadzone_px * 40);
            int16_t f_pitch = (abs(cal_pitch) > dynamic_deadzone) ? cal_pitch : 0;
            int16_t f_yaw   = (abs(cal_yaw)   > dynamic_deadzone) ? cal_yaw   : 0;
            
            // D. 阻尼衰减转换
            float target_dx = (float)f_yaw / DAMPING;
            float target_dy = (float)f_pitch / DAMPING;
            
            // E. 一阶滞后低通平滑滤波 (Low-Pass Filter)
            // 公式：Smooth_k = Smooth_{k-1} + Alpha * (Target - Smooth_{k-1})
            smooth_dx = smooth_dx + FILTER_ALPHA * (target_dx - smooth_dx);
            smooth_dy = smooth_dy + FILTER_ALPHA * (target_dy - smooth_dy);
            
            // F. 应用用户全局增益放大倍数并强转整型像素
            int16_t move_x = (int16_t)(smooth_dx * g_cfg.air_gain);
            int16_t move_y = (int16_t)(smooth_dy * g_cfg.air_gain);
            
            // G. 输出级像素死区截断：如果移动距离不足设定像素，强行归零（彻底根除微弱手抖）
            if (abs(move_x) <= g_cfg.deadzone_px) move_x = 0;
            if (abs(move_y) <= g_cfg.deadzone_px) move_y = 0;
            
            // H. 发送蓝牙 HID 报告
            if (move_x != 0 || move_y != 0) {
                BleMouseDevice.move(move_x, move_y, 0);
            }
            
            // 同步给 OLED 显示
            oled_mouse_x = move_x; 
            oled_mouse_y = move_y;
        }
    } else {
        // 滚轮锁定模式下，光标输出清零
        oled_mouse_x = 0; 
        oled_mouse_y = 0;
    }
}
```

---

## 4. 关键算法数学模型与设计亮点归纳

### 4.1 双级死区架构（Dual Deadzone System）
* **一级死区（原始传感器层）**：
  $$\text{Threshold}_{\text{raw}} = \text{deadzone\_px} \times 40$$
  * 作用于从 MPU6050 刚读出的 16 位整型数据，目的是在进行浮点运算和除以 120 的阻尼衰减前，**彻底切断 MPU6050 内部热敏白噪声引起的最低有效位（LSB）翻转**，避免无意义的浮点运算开销。
* **二级死区（屏幕像素层）**：
  $$\text{if } \vert{}move\vert{} \le \text{deadzone\_px} \implies move = 0$$
  * 作用于低通滤波和 `air_gain` 放大之后的最终整型，目的是**消除人体肌肉在紧握或点击按键时的生理性微颤动**，保证用户在单击按键时鼠标指针绝对稳固，不产生位移偏移。

### 4.2 一阶低通滤波器设计（First-Order Low-Pass Filter）
代码中采用的滤波方程为：
$$\text{Output}_k = \text{Output}_{k-1} + \alpha \cdot (\text{Input}_k - \text{Output}_{k-1})$$
* **为什么 $\alpha = 0.4$**：
  * 若 $\alpha \to 1.0$：无平滑效果，光标会有明显的陀螺仪阶梯锯齿感。
  * 若 $\alpha \le 0.1$：极其平滑，但光标会有明显的“拖拽感/迟滞感”（类似在泥浆中移动）。
  * 取值 $0.4$ 是在高频 83Hz 采样下兼顾**跟手性**与**平滑轨迹**的最佳平衡点。


# MPU6050 底层 I2C 硬件驱动模块学习笔记 (`mpu6050_driver`)

## 1. 模块定位与职责

该模块属于系统的最底层驱动层（BSP / HAL 层），直接使用硬件 I2C 控制器与物理传感器通信，主要职责包括：
1. **硬件就绪验证**：开机核验传感器物理通信与器件 ID。
2. **寄存器工作态配置**：唤醒芯片、配置量程与硬件滤波参数。
3. **高效连读打包**：采用单次 Burst Read 读取 14 字节连续寄存器，并做大端字节序拼接，生成结构化物理原始数据。

```
+-------------------------------------------------------+
| 姿态/飞鼠上层算法 (imu_processing.c / mouse_mode.cpp)  |
+---------------------------+---------------------------+
                            | 调用 mpu6050_read_raw()
                            v
+-------------------------------------------------------+
|          MPU6050 底层驱动 (mpu6050_driver)             | <--- [当前模块]
+---------------------------+---------------------------+
                            | 400kHz I2C 连续读写
                            v
+-------------------------------------------------------+
|                   MPU6050 传感器芯片                  |
+-------------------------------------------------------+
```

---

## 2. 数据结构与寄存器映射

### 2.1 结构体内存对齐 (`mpu6050_raw_data_t`)
```c
typedef struct {
    int16_t accel_x; // 0x3B (高) / 0x3C (低)
    int16_t accel_y; // 0x3D (高) / 0x3E (低)
    int16_t accel_z; // 0x3F (高) / 0x40 (低)
    int16_t temp;    // 0x41 (高) / 0x42 (低) [内部温度计]
    int16_t gyro_x;  // 0x43 (高) / 0x44 (低) [Roll 轴]
    int16_t gyro_y;  // 0x45 (高) / 0x46 (低) [Pitch 轴]
    int16_t gyro_z;  // 0x47 (高) / 0x48 (低) [Yaw 轴]
} mpu6050_raw_data_t;
```
* **内存连续性**：共 7 个 `int16_t`，合计连续占用 14 字节，与 MPU6050 内部从寄存器 `0x3B` 到 `0x48` 的物理存储完全一一对应。
* **上层指针复用**：这也是为什么在上层飞鼠代码中能够直接使用 `(int16_t*)&temp_data` 并通过 `ptr[5]`、`ptr[6]` 精准索引用到 Pitch 和 Yaw 轴的硬件基础。

---

## 3. 关键寄存器与硬件初始化逻辑

### 3.1 驱动初始化步骤解析 (`mpu6050_init`)

```c
bool mpu6050_init(void) {
    // 1. 引脚配置与超频提升吞吐
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    Wire.setClock(400000); // 400kHz 快速模式 (I2C Fast Mode)

    // 2. 自检验证 WHO_AM_I
    Wire.beginTransmission(MPU6050_ADDR);
    Wire.write(MPU6050_WHO_AM_I);
    Wire.endTransmission(false); // Restart 信号，不释放总线
    Wire.requestFrom((uint16_t)MPU6050_ADDR, (uint8_t)1);
    if (!Wire.available() || Wire.read() != 0x68) return false;

    // 3. 解除休眠 (Power Management 1)
    write_register(MPU6050_PWR_MGMT_1, 0x00);

    // 4. 陀螺仪量程配置 (±500°/s)
    write_register(MPU6050_GYRO_CONFIG, 0x08);

    // 5. 加速度计量程配置 (±2g)
    write_register(MPU6050_ACCEL_CONFIG, 0x00);

    // 6. 开启片上硬件数字低通滤波器 (DLPF)
    write_register(MPU6050_CONFIG, 0x03);

    return true;
}
```

### 3.2 寄存器参数设置深度对照

| 寄存器名 | 地址 | 写入值 | 硬件行为与工程考量 |
| :--- | :--- | :--- | :--- |
| `PWR_MGMT_1` | `0x6B` | `0x00` | **唤醒芯片**。MPU6050 上电默认处于 SLEEP 模式（省电停摆），写 0 唤醒内部振荡器与 ADC。 |
| `GYRO_CONFIG`| `0x1B` | `0x08` | `FS_SEL = 1`（即第 3 位置 1）。设定量程为 **$\pm 500^\circ/\text{s}$**，灵敏度严格匹配算法层的 `GYRO_SCALE (65.5f)`。 |
| `ACCEL_CONFIG`| `0x1C` | `0x00`| `AFS_SEL = 0`。设定量程为最高精度的 **$\pm 2g$**，灵敏度严格匹配算法层的 `ACCEL_SCALE (16384.0f)`。 |
| `CONFIG` | `0x1A` | `0x03` | `DLPF_CFG = 3`。开启芯片内部低通滤波器，将陀螺仪带宽截断至 **44Hz**（延迟约 4.8ms），在硬件采集端提前吸收按键点击或电机震动的机械高频噪声。 |

---

## 4. 高性能连续读取机制 (`mpu6050_read_raw`)

### 4.1 核心代码
```c
void mpu6050_read_raw(mpu6050_raw_data_t *data) {
    if (data == nullptr) return;

    Wire.beginTransmission(MPU6050_ADDR);
    Wire.write(MPU6050_ACCEL_XOUT_H); // 写入起始地址 0x3B
    Wire.endTransmission(false);        // 发送 Repeated Start，维持总线占用

    Wire.requestFrom((uint16_t)MPU6050_ADDR, (uint8_t)14); // 突发读取 14 字节

    if (Wire.available() == 14) {
        // 大端模式拼接：(MSB << 8) | LSB
        data->accel_x = (int16_t)((Wire.read() << 8) | Wire.read());
        data->accel_y = (int16_t)((Wire.read() << 8) | Wire.read());
        data->accel_z = (int16_t)((Wire.read() << 8) | Wire.read());
        data->temp    = (int16_t)((Wire.read() << 8) | Wire.read());
        data->gyro_x  = (int16_t)((Wire.read() << 8) | Wire.read());
        data->gyro_y  = (int16_t)((Wire.read() << 8) | Wire.read());
        data->gyro_z  = (int16_t)((Wire.read() << 8) | Wire.read());
    }
}
```

### 4.2 为什么必须使用 `Wire.endTransmission(false)`（重启动信号）？
* 在标准 I2C 通信中，如果传入 `true`，主机会发出 **STOP 条件** 释放总线。当多个从机挂载在总线上时，这会给其他设备插队的机会。
* 传入 `false` 会发出 **RESTART 条件**，主机持续霸占总线并立即将从机切换为接收模式（`requestFrom`），防止通信被意外打断。

### 4.3 为什么必须一次性读 14 字节（Burst Read）？
1. **防止数据撕裂（Data Tearing）**：
   * MPU6050 的高低字节以及不同轴的数据是在同一个内部时钟周期更新锁存的。
   * 如果分 7 次单独读取，可能出现刚读完高 8 位，芯片内部就更新了传感器，再读低 8 位就会拼出荒谬的跳变错误值。一次性连读可在内部影子寄存器中锁定同一次采样的完整快照。
2. **极大提升传输效率**：
   * 每次单独发寄存器地址都有开销（Start + 寻址 + ACK + Stop）。
   * 采用连续突发读取，只需发一次起始地址 `0x3B`，内部地址计数器会自动自增，直接读空 14 个字节，节省约 60% 的总线通信耗时。