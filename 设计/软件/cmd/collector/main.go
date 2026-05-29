package main

import (
	"bytes"
	"encoding/binary"
	"errors"
	"encoding/json"
	"fmt"
	"log"
	"net/http"
	"os"
	"os/signal"
	"sync"
	"syscall"
	"time"
	"unsafe"

	"github.com/cilium/ebpf"
	"github.com/cilium/ebpf/link"
	"github.com/cilium/ebpf/ringbuf"
	"github.com/cilium/ebpf/rlimit"
)

// ============================================================
// Event types matching BPF C structures
// ============================================================

type LoopEvent struct {
	TsNs           uint64
	ActualInterval uint64
	ExpectedNs     uint64
	JitterNs       uint64
	PID            int32
	Severity       uint8
	Pad            [3]uint8
}

type SerialEvent struct {
	TsNs      uint64
	Bytes     uint32
	TTYIndex  uint16
	Dir       uint8
	Severity  uint8
	LatencyUs uint32
}

type SchedEvent struct {
	TsNs      uint64
	PID       int32
	_         uint32    // padding (C struct alignment)
	WaitNs    uint64
	RuntimeNs uint64
	Prio      uint32
	Comm      [16]byte
}

// ============================================================
// Jitter history ring buffer for dashboard chart
type JitterPoint struct {
	Time   int64   `json:"time"`
	Jitter float64 `json:"jitter"`
}

const maxJitterHistory = 300 // 5 minutes at 1 point/sec
// ============================================================

type SafetyStore struct {
	mu sync.RWMutex

	// Control loop stats
	LoopWarnings   int
	LoopCriticals  int
	LastJitterUs   float64
	MaxJitterUs    float64

	// Serial stats
	SerialTXBytes  uint64
	SerialRXBytes  uint64
	SerialStalls   int
	LastSerialLatMs float64

	// Scheduling stats
	SchedEvents    int
	MaxWaitMs      float64
	AvgWaitMs      float64

	// Robot telemetry (from Python control node)
	RobotTelemetry *RobotTelemetry

	// Safety command queue (read by Python control node)
	SafetyCommand string

	// Jitter history for dashboard chart (real data, not random)
	JitterHistory []JitterPoint

	// History
	LoopEvents   []LoopEvent
	SerialEvents []SerialEvent
	SchedEvents_ []SchedEvent
	Alerts       []Alert

	maxHistory int
}

type Alert struct {
	Time    time.Time `json:"time"`
	Type    string    `json:"type"`
	Level   string    `json:"level"`
	Message string    `json:"message"`
	Value   float64   `json:"value"`
}

// Robot telemetry received from Python control node
type RobotTelemetry struct {
	PID           int     `json:"pid"`
	YawDeg        float64 `json:"yaw_deg"`
	MotorLeft     int     `json:"motor_left"`
	MotorRight    int     `json:"motor_right"`
	EmergencyStop bool    `json:"emergency_stop"`
	JitterUs      float64 `json:"jitter_us"`
	MissedCycles  int     `json:"missed_cycles"`
	CycleCount    int     `json:"cycle_count"`
	LastUpdate    time.Time
}

func NewSafetyStore() *SafetyStore {
	return &SafetyStore{
		LoopEvents:   make([]LoopEvent, 0, 500),
		SerialEvents: make([]SerialEvent, 0, 500),
		SchedEvents_: make([]SchedEvent, 0, 500),
		Alerts:       make([]Alert, 0, 200),
		maxHistory:   500,
	}
}

func (s *SafetyStore) AddLoopEvent(e LoopEvent) {
	s.mu.Lock()
	defer s.mu.Unlock()

	jitterUs := float64(e.JitterNs) / 1000.0
	s.LastJitterUs = jitterUs
	if jitterUs > s.MaxJitterUs {
		s.MaxJitterUs = jitterUs
	}

	if e.Severity == 1 {
		s.LoopWarnings++
		s.Alerts = append(s.Alerts, Alert{
			Time: time.Now(), Type: "loop_jitter", Level: "warning",
			Message: fmt.Sprintf("Control loop jitter %.0fus exceeds warning threshold", jitterUs),
			Value: jitterUs,
		})
	} else if e.Severity == 2 {
		s.LoopCriticals++
		s.Alerts = append(s.Alerts, Alert{
			Time: time.Now(), Type: "loop_jitter", Level: "critical",
			Message: fmt.Sprintf("CRITICAL: Control loop jitter %.0fus — possible missed cycle", jitterUs),
			Value: jitterUs,
		})
	}

	// Record jitter history for dashboard (1 point per event)
	s.JitterHistory = append(s.JitterHistory, JitterPoint{
		Time:   time.Now().UnixMilli(),
		Jitter: jitterUs,
	})
	if len(s.JitterHistory) > maxJitterHistory {
		s.JitterHistory = s.JitterHistory[1:]
	}

	s.LoopEvents = append(s.LoopEvents, e)
	if len(s.LoopEvents) > s.maxHistory { s.LoopEvents = s.LoopEvents[1:] }
	if len(s.Alerts) > s.maxHistory { s.Alerts = s.Alerts[1:] }
}

func (s *SafetyStore) AddSerialEvent(e SerialEvent) {
	s.mu.Lock()
	defer s.mu.Unlock()

	if e.Dir == 0 { s.SerialTXBytes += uint64(e.Bytes) } else { s.SerialRXBytes += uint64(e.Bytes) }
	s.LastSerialLatMs = float64(e.LatencyUs) / 1000.0

	if e.Severity >= 2 {
		s.SerialStalls++
		dirStr := "TX"
		if e.Dir == 1 { dirStr = "RX" }
		lvl := "warning"
		if e.Severity == 2 { lvl = "critical" }
		s.Alerts = append(s.Alerts, Alert{
			Time: time.Now(), Type: "serial_stall", Level: lvl,
			Message: fmt.Sprintf("Serial %s stall detected — %.1fms gap (tty#%d)", dirStr, s.LastSerialLatMs, e.TTYIndex),
			Value: s.LastSerialLatMs,
		})
		if len(s.Alerts) > s.maxHistory { s.Alerts = s.Alerts[1:] }
	}

	s.SerialEvents = append(s.SerialEvents, e)
	if len(s.SerialEvents) > s.maxHistory { s.SerialEvents = s.SerialEvents[1:] }
}

func (s *SafetyStore) AddSchedEvent(e SchedEvent) {
	s.mu.Lock()
	defer s.mu.Unlock()

	s.SchedEvents++
	waitMs := float64(e.WaitNs) / 1e6
	s.AvgWaitMs = (s.AvgWaitMs*float64(s.SchedEvents-1) + waitMs) / float64(s.SchedEvents)
	if waitMs > s.MaxWaitMs { s.MaxWaitMs = waitMs }

	if waitMs > 10.0 { // >10ms scheduling latency
		s.Alerts = append(s.Alerts, Alert{
			Time: time.Now(), Type: "sched_latency", Level: "warning",
			Message: fmt.Sprintf("PID %d scheduling latency %.1fms", e.PID, waitMs),
			Value: waitMs,
		})
		if len(s.Alerts) > s.maxHistory { s.Alerts = s.Alerts[1:] }
	}

	s.SchedEvents_ = append(s.SchedEvents_, e)
	if len(s.SchedEvents_) > s.maxHistory { s.SchedEvents_ = s.SchedEvents_[1:] }
}

// ============================================================
// HTTP API
// ============================================================

// BPF map for PID registration (loop_monitor)
var loopMonitoredPIDs *ebpf.Map

type API struct {
	store *SafetyStore
	addr  string
}

func NewAPI(store *SafetyStore) *API { return &API{store: store, addr: ":8090"} }

func (a *API) Start() error {
	mux := http.NewServeMux()
	mux.HandleFunc("/api/summary",          a.handleSummary)
	mux.HandleFunc("/api/loop",             a.handleLoop)
	mux.HandleFunc("/api/serial",           a.handleSerial)
	mux.HandleFunc("/api/sched",            a.handleSched)
	mux.HandleFunc("/api/alerts",           a.handleAlerts)
	mux.HandleFunc("/api/robot_telemetry",  a.handleRobotTelemetry)
	mux.HandleFunc("/api/monitor_pid",     a.handleMonitorPID)
	mux.HandleFunc("/api/command",          a.handleCommand)
	mux.HandleFunc("/api/safety_command",   a.handleSafetyCommand)
	mux.HandleFunc("/api/jitter_history",  a.handleJitterHistory)

	// Serve frontend
	if _, err := os.Stat("frontend/dist"); err == nil {
		mux.Handle("/", http.FileServer(http.Dir("frontend/dist")))
	}

	handler := corsMiddleware(mux)
	log.Printf("[api] Robot Safety Monitor on %s", a.addr)
	return http.ListenAndServe(a.addr, handler)
}

func (a *API) handleSummary(w http.ResponseWriter, r *http.Request) {
	a.store.mu.RLock()
	defer a.store.mu.RUnlock()
	resp := map[string]interface{}{
		"loop_warnings":   a.store.LoopWarnings,
		"loop_criticals":  a.store.LoopCriticals,
		"last_jitter_us":  a.store.LastJitterUs,
		"max_jitter_us":   a.store.MaxJitterUs,
		"serial_tx_bytes": a.store.SerialTXBytes,
		"serial_rx_bytes": a.store.SerialRXBytes,
		"serial_stalls":   a.store.SerialStalls,
		"sched_events":    a.store.SchedEvents,
		"avg_wait_ms":     a.store.AvgWaitMs,
		"max_wait_ms":     a.store.MaxWaitMs,
		"robot_safety":    safetyStatus(a.store),
		"timestamp":       time.Now().UnixMilli(),
	}
	if a.store.RobotTelemetry != nil {
		resp["robot_yaw"]     = a.store.RobotTelemetry.YawDeg
		resp["robot_motor_l"] = a.store.RobotTelemetry.MotorLeft
		resp["robot_motor_r"] = a.store.RobotTelemetry.MotorRight
		resp["robot_estop"]   = a.store.RobotTelemetry.EmergencyStop
	}
	json.NewEncoder(w).Encode(resp)
}

func (a *API) handleLoop(w http.ResponseWriter, r *http.Request) {
	a.store.mu.RLock()
	defer a.store.mu.RUnlock()
	json.NewEncoder(w).Encode(a.store.LoopEvents[len(a.store.LoopEvents)-min(100, len(a.store.LoopEvents)):])
}

func (a *API) handleSerial(w http.ResponseWriter, r *http.Request) {
	a.store.mu.RLock()
	defer a.store.mu.RUnlock()
	json.NewEncoder(w).Encode(a.store.SerialEvents[len(a.store.SerialEvents)-min(100, len(a.store.SerialEvents)):])
}

func (a *API) handleSched(w http.ResponseWriter, r *http.Request) {
	a.store.mu.RLock()
	defer a.store.mu.RUnlock()
	json.NewEncoder(w).Encode(a.store.SchedEvents_[len(a.store.SchedEvents_)-min(50, len(a.store.SchedEvents_)):])
}

func (a *API) handleAlerts(w http.ResponseWriter, r *http.Request) {
	a.store.mu.RLock()
	defer a.store.mu.RUnlock()
	json.NewEncoder(w).Encode(a.store.Alerts[len(a.store.Alerts)-min(50, len(a.store.Alerts)):])
}

func safetyStatus(s *SafetyStore) string {
	if s.LoopCriticals > 0 { return "CRITICAL" }
	if s.SerialStalls > 2 || s.LoopWarnings > 10 { return "WARNING" }
	return "NOMINAL"
}

func (a *API) handleRobotTelemetry(w http.ResponseWriter, r *http.Request) {
	if r.Method != "POST" {
		http.Error(w, "POST only", 405)
		return
	}
	var tlm RobotTelemetry
	if err := json.NewDecoder(r.Body).Decode(&tlm); err != nil {
		http.Error(w, "bad JSON", 400)
		return
	}
	tlm.LastUpdate = time.Now()
	a.store.mu.Lock()
	a.store.RobotTelemetry = &tlm
	// Record robot-reported jitter for dashboard chart
	if tlm.JitterUs > 0 {
		a.store.JitterHistory = append(a.store.JitterHistory, JitterPoint{
			Time:   time.Now().UnixMilli(),
			Jitter: tlm.JitterUs,
		})
		if len(a.store.JitterHistory) > maxJitterHistory {
			a.store.JitterHistory = a.store.JitterHistory[1:]
		}
	}
	a.store.mu.Unlock()
	w.WriteHeader(200)
}

func (a *API) handleMonitorPID(w http.ResponseWriter, r *http.Request) {
	if r.Method != "POST" {
		http.Error(w, "POST only", 405)
		return
	}
	var body struct {
		PID int32 `json:"pid"`
	}
	if err := json.NewDecoder(r.Body).Decode(&body); err != nil || body.PID <= 0 {
		http.Error(w, "bad JSON or missing pid", 400)
		return
	}
	if loopMonitoredPIDs != nil {
		one := uint8(1)
		if err := loopMonitoredPIDs.Put(unsafe.Pointer(&body.PID), unsafe.Pointer(&one)); err != nil {
			log.Printf("[monitor] failed to register PID %d: %v", body.PID, err)
			http.Error(w, "BPF write failed", 500)
			return
		}
		log.Printf("[monitor] Registered PID %d for loop monitoring", body.PID)
	}
	w.WriteHeader(200)
	json.NewEncoder(w).Encode(map[string]string{"status": "ok"})
}

func (a *API) handleCommand(w http.ResponseWriter, r *http.Request) {
	if r.Method != "POST" {
		http.Error(w, "POST only", 405)
		return
	}
	var body struct {
		Cmd string `json:"cmd"`
	}
	if err := json.NewDecoder(r.Body).Decode(&body); err != nil {
		http.Error(w, "bad JSON", 400)
		return
	}
	a.store.mu.Lock()
	a.store.SafetyCommand = body.Cmd
	a.store.mu.Unlock()
	log.Printf("[safety] Queued command: %s", body.Cmd)
	w.WriteHeader(200)
}

func (a *API) handleJitterHistory(w http.ResponseWriter, r *http.Request) {
	a.store.mu.RLock()
	defer a.store.mu.RUnlock()
	json.NewEncoder(w).Encode(a.store.JitterHistory)
}

func (a *API) handleSafetyCommand(w http.ResponseWriter, r *http.Request) {
	a.store.mu.Lock()
	cmd := a.store.SafetyCommand
	a.store.SafetyCommand = "" // atomic read + clear
	a.store.mu.Unlock()
	json.NewEncoder(w).Encode(map[string]string{"cmd": cmd})
}

// safetyMonitor watches for CRITICAL status and issues emergency stop
func safetyMonitor(store *SafetyStore) {
	wasCritical := false
	ticker := time.NewTicker(500 * time.Millisecond)
	defer ticker.Stop()
	for range ticker.C {
		store.mu.RLock()
		status := safetyStatus(store)
		store.mu.RUnlock()

		if status == "CRITICAL" && !wasCritical {
			store.mu.Lock()
			store.SafetyCommand = "ESTOP"
			store.mu.Unlock()
			log.Println("[safety] AUTO EMERGENCY STOP — critical safety violation detected")
			wasCritical = true
		} else if status == "NOMINAL" {
			wasCritical = false
		}
	}
}

func corsMiddleware(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Access-Control-Allow-Origin", "*")
		w.Header().Set("Access-Control-Allow-Methods", "GET, OPTIONS")
		if r.Method == "OPTIONS" { w.WriteHeader(204); return }
		next.ServeHTTP(w, r)
	})
}

func min(a, b int) int { if a < b { return a }; return b }

// ============================================================
// BPF loading helpers
// ============================================================

func loadBPFObjects(objPath string) (*ebpf.Collection, error) {
	spec, err := ebpf.LoadCollectionSpec(objPath)
	if err != nil { return nil, fmt.Errorf("load spec %s: %w", objPath, err) }
	coll, err := ebpf.NewCollection(spec)
	if err != nil {
		var ve *ebpf.VerifierError
		if errors.As(err, &ve) { log.Printf("BPF verifier error:\n%s", ve) }
		return nil, fmt.Errorf("new collection %s: %w", objPath, err)
	}
	return coll, nil
}

func attachKprobe(coll *ebpf.Collection, fnName, progName string) (link.Link, error) {
	prog, ok := coll.Programs[progName]
	if !ok { return nil, fmt.Errorf("program %s not found in collection", progName) }
	return link.Kprobe(fnName, prog, nil)
}

func attachTracepoint(coll *ebpf.Collection, group, name, progName string) (link.Link, error) {
	prog, ok := coll.Programs[progName]
	if !ok { return nil, fmt.Errorf("program %s not found in collection", progName) }
	return link.Tracepoint(group, name, prog, nil)
}

// ============================================================
// Main
// ============================================================

func main() {
	if err := run(); err != nil { log.Fatalf("Fatal: %v", err) }
}

func run() error {
	if err := rlimit.RemoveMemlock(); err != nil { return err }

	bpfDir := os.Getenv("BPF_DIR")
	if bpfDir == "" { bpfDir = "../../bpf" }

	store := NewSafetyStore()
	api := NewAPI(store)

	go func() {
		if err := api.Start(); err != nil {
			log.Printf("[api] %v", err)
		}
	}()

	// Load: Loop Monitor
	loopColl, err := loadBPFObjects(bpfDir + "/loop_monitor.bpf.o")
	if err != nil {
		log.Printf("[warn] loop_monitor not available: %v (skip)", err)
	} else {
		defer loopColl.Close()
		// Attach both nanosleep and clock_nanosleep tracepoints.
		// Python 3.11+ uses clock_nanosleep on Linux 5.1+.
		attached := 0
		lp1, err1 := attachTracepoint(loopColl, "syscalls", "sys_enter_nanosleep", "tp_nanosleep")
		if err1 == nil { defer lp1.Close(); attached++ }
		lp2, err2 := attachTracepoint(loopColl, "syscalls", "sys_enter_clock_nanosleep", "tp_clock_nanosleep")
		if err2 == nil { defer lp2.Close(); attached++ }
		if attached > 0 {
			log.Println("[collector] loop_monitor active")
		} else {
			log.Printf("[warn] loop_monitor attach failed: nanosleep=%v clock_nanosleep=%v", err1, err2)
		}
		// Set safety thresholds (Allan-variance calibrated)
		//
		// Derived from STM32-MPU6050-System project:
		//   MPU6050 Z-axis: ARW = 6.021 deg/sqrt(h), RRW = 6.311 (deg/s)/sqrt(h)
		//   STM32F103 DWT-measured acquisition jitter: < 100 us
		//   Control loop period: 10 ms (100 Hz)
		//   Allan-optimized R/Q = 0.91 reduces RMSE by 2.3-2.7x
		//
		// Threshold derivation:
		//   WARNING (500 us): ~5x the STM32 hard-real-time jitter baseline.
		//     Linux-to-STM32 command timing uncertainty exceeds the
		//     Allan-calibrated sensor fusion noise floor. PID KD term
		//     (0.15) error approx 5% — filter tracks but precision degrades.
		//
		//   CRITICAL (2000 us): 20% of the 10 ms control period.
		//     Effective control rate drops to ~83 Hz. PID KD term error
		//     approx 20% — derivative action becomes unreliable. The thesis
		//     shows this level of timing error can nullify Allan optimization
		//     benefit (RMSE returns to empirical-default baseline).
		//
		//   Expected period (10000000 ns): 10 ms = 100 Hz control loop.
		//
		// Ref: Xing Dong. Allan Variance-Based MEMS Gyroscope Noise Analysis
		//      and Adaptive Kalman Filtering (2026, Undergraduate Thesis)
		if m, ok := loopColl.Maps["safety_config"]; ok {
			_ = m.Put(uint32(0), uint64(500000))   // WARNING  threshold (ns)
			_ = m.Put(uint32(1), uint64(2000000))  // CRITICAL threshold (ns)
			_ = m.Put(uint32(2), uint64(10000000)) // Expected period (ns)
		}
		// Assign monitored_pids map for PID registration API
		if m, ok := loopColl.Maps["monitored_pids"]; ok {
			loopMonitoredPIDs = m
			log.Println("[collector] PID registration enabled")
		}
		// Ring buffer consumer
		if events, ok := loopColl.Maps["loop_events"]; ok {
			go consumeLoop(events, store)
		}
	}

	// Load: Serial Monitor
	serialColl, err := loadBPFObjects(bpfDir + "/serial_monitor.bpf.o")
	if err != nil {
		log.Printf("[warn] serial_monitor not available: %v (skip)", err)
	} else {
		defer serialColl.Close()
		sp1, err1 := attachKprobe(serialColl, "tty_write", "kp_tty_write")
		sp2, err2 := attachKprobe(serialColl, "tty_read",  "kp_tty_read")
		if err1 != nil { log.Printf("[warn] serial kp_tty_write: %v", err1) }
		if err2 != nil { log.Printf("[warn] serial kp_tty_read: %v", err2) }
		if err1 == nil { defer sp1.Close() }
		if err2 == nil { defer sp2.Close() }
		if err1 == nil || err2 == nil {
			log.Println("[collector] serial_monitor active")
		}
		if events, ok := serialColl.Maps["serial_events"]; ok {
			go consumeSerial(events, store)
		}
	}

	// Load: Sched Monitor
	schedColl, err := loadBPFObjects(bpfDir + "/sched_monitor.bpf.o")
	if err != nil {
		log.Printf("[warn] sched_monitor not available: %v (skip)", err)
	} else {
		defer schedColl.Close()
		stp1, err1 := attachTracepoint(schedColl, "sched", "sched_switch", "tp_sched_switch")
		stp2, err2 := attachTracepoint(schedColl, "sched", "sched_wakeup", "tp_wakeup")
		if err1 != nil { log.Printf("[warn] sched_switch: %v", err1) } else { defer stp1.Close() }
		if err2 != nil { log.Printf("[warn] sched_wakeup: %v", err2) } else { defer stp2.Close() }
		if err1 == nil || err2 == nil {
			log.Println("[collector] sched_monitor active")
		}
		if events, ok := schedColl.Maps["sched_events"]; ok {
			go consumeSched(events, store)
		}
	}

	// Start safety auto-response monitor
	go safetyMonitor(store)

	sigCh := make(chan os.Signal, 1)
	signal.Notify(sigCh, os.Interrupt, syscall.SIGTERM)
	log.Println("[collector] Robot Safety Monitor running — API on :8090")
	<-sigCh
	log.Println("[collector] Shutting down...")
	return nil
}

func consumeLoop(m *ebpf.Map, store *SafetyStore) {
	rd, err := ringbuf.NewReader(m)
	if err != nil { log.Printf("[loop] ringbuf error: %v", err); return }
	defer rd.Close()
	for {
		rec, err := rd.Read()
		if err != nil { return }
		var e LoopEvent
		binary.Read(bytes.NewReader(rec.RawSample), binary.LittleEndian, &e)
		store.AddLoopEvent(e)
	}
}

func consumeSerial(m *ebpf.Map, store *SafetyStore) {
	rd, err := ringbuf.NewReader(m)
	if err != nil { log.Printf("[serial] ringbuf error: %v", err); return }
	defer rd.Close()
	for {
		rec, err := rd.Read()
		if err != nil { return }
		var e SerialEvent
		binary.Read(bytes.NewReader(rec.RawSample), binary.LittleEndian, &e)
		store.AddSerialEvent(e)
	}
}

func consumeSched(m *ebpf.Map, store *SafetyStore) {
	rd, err := ringbuf.NewReader(m)
	if err != nil { log.Printf("[sched] ringbuf error: %v", err); return }
	defer rd.Close()
	for {
		rec, err := rd.Read()
		if err != nil { return }
		var e SchedEvent
		binary.Read(bytes.NewReader(rec.RawSample), binary.LittleEndian, &e)
		store.AddSchedEvent(e)
	}
}
