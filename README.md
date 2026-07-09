# AlgoHawkes — Multivariate Hawkes-Based Cross-Exchange Order-Flow Analytics (WORK IN PROGRESS)

## Abstract

**AlgoHawkes** is a research-oriented, high-performance C/C++ platform designed to
model, monitor, and analyse the *self- and mutually-exciting* dynamics of
cryptocurrency order flow across multiple exchanges in real time.

The system ingests live trade streams from several public exchanges
(Binance, Coinbase, Kraken, OKX, Bybit), normalises them into a common
representation, and treats them as realisations of a **multivariate Hawkes
point process**. The parameters of this process — background intensities,
excitation kernels and decay rates — are calibrated online via a parallel
worker pool and rendered through a Bloomberg-style graphical terminal built on
Dear ImGui / ImPlot.

The project's ambition is twofold: (i) to provide an academic, transparent
testbed for the empirical study of cross-market microstructure and lead–lag
propagation; and (ii) to expose an engineering blueprint of a low-latency,
lock-safe, multi-threaded data pipeline suitable for further research in
high-frequency econometrics.

---

## Disclaimer

AlgoHawkes is a **solo student project**, developed by a single author who is
still in the course of his Master's studies in Statistical and Stochastic
Modelling. The code base has been written entirely from scratch and has
not been reviewed by industry engineers.

Consequently, the reader should be aware that:

- the software may still contain **design flaws, architectural
  inconsistencies and non-idiomatic C++ constructs**;
- several subsystems are known to require **refactoring** — in particular,
  the coupling between the scheduler, the workers and the telemetry
  layer, as well as the ownership semantics around the shared queues;
- performance-critical paths (the recursive intensity update, the
  Nelder–Mead simplex, the residual accumulation) have not yet been
  systematically **profiled or optimised**, and some allocations on the
  hot path could be eliminated;
- portions of the code retain original French comments and mixed naming
  conventions inherited from earlier prototypes.

⚠️ For the moment, this project only work on **MacOS** with the **Apple Silicon architecture**. On these processors, there is more restrictions for multi-threading and multi-coring, so it work differently.

---

## 1. Scientific Motivation

Trades on modern electronic exchanges arrive in *bursts*: a transaction on
one venue significantly raises the short-term probability of subsequent
transactions, both on the same venue and on correlated venues. This
clustering behaviour is poorly captured by Poisson-type assumptions and is
naturally described by the class of **Hawkes self- and mutually-exciting
point processes**, whose application to high-frequency finance is
surveyed by Laub, Taimre & Pollett (2015) and Embrechts, Liniger & Lin
(2011). AlgoHawkes provides a real-time implementation of the multivariate
Hawkes framework with an exponential kernel, together with an online
diagnostic based on residual analysis.

### 1.1 Multivariate Hawkes process

Consider a collection of $m$ simple counting processes
$\mathbf{N} = \{N_1(\cdot), \dots, N_m(\cdot)\}$ on the interval $[0,T]$,
where each dimension $i$ indexes one exchange. Let
$\{t_l^{(j)} : j \in \{1,\dots,m\},\, l \in \mathbb{N}\}$ denote the
observed arrival times. The process $\mathbf{N}$ is called a *multivariate
Hawkes process* when, for every $i \in \{1,\dots,m\}$, the conditional
intensity of the $i$-th component admits the representation

$$
\lambda_i^*(t) \;=\; \lambda_i \;+\; \sum_{j=1}^{m} \sum_{t_l^{(j)} < t}
\Phi_{\theta_{j\to i}}\!\bigl(t - t_l^{(j)}\bigr),
$$

with $\lambda_i > 0$ the **background intensity** of dimension $i$ and
$\Phi_{\theta_{j\to i}} : [0,+\infty) \to [0,+\infty)$ the **excitation
kernel** encoding how past events on dimension $j$ influence the future
intensity on dimension $i$.

Throughout the platform, following Filimonov and Sornette (2015), we
adopt the **exponential kernel**

$$
\Phi_{\theta_{j\to i}}(t) \;=\; \alpha_{j,i}\, \exp\!\bigl(-\beta_{j,i}\, t\bigr),
\qquad \alpha_{j,i} \geq 0,\ \beta_{j,i} > 0,
$$

whose parameters admit an intuitive interpretation: $\alpha_{j,i}$ is the
**excitation strength** of dimension $j$ onto $i$, while
$\beta_{j,i}$ is the associated **memory-decay rate**. The
branching-ratio matrix $\Gamma_{j,i} = \alpha_{j,i} / \beta_{j,i}$
governs stationarity: its spectral radius must remain strictly below one
for the process to be well-defined on $\mathbb{R}_+$.

### 1.2 Log-likelihood and separated optimisation

For dimension $i$, the log-likelihood on $[0,T]$ takes the form

$$
\log \mathcal{L}_i
\;=\; \sum_{p=1}^{k_i} \log\!\bigl(\lambda_i^*(t_p^{(i)})\bigr)
      \;-\; \lambda_i T
      \;-\; \sum_{j=1}^{m}
             \sum_{l=1}^{k_j}
             \int_{t_l^{(j)}}^{T}
             \Phi_{\theta_{j\to i}}\!\bigl(u - t_l^{(j)}\bigr)\, du.
$$

Direct joint maximisation over $(\lambda_i, \alpha_{\cdot,i}, \beta_{\cdot,i})$
is numerically ill-conditioned. Following the two-step scheme of Lyubushin
& Pisarenko (1994) and Filimonov & Sornette (2015), the calibration
routine of AlgoHawkes exploits the first-order optimality condition

$$
\hat{\lambda}_i \, T \;+\; \sum_{j=1}^{m} G_j\!\bigl(T,\hat{\theta}_{j\to i}\bigr) \;=\; k_i,
$$

which yields a closed-form expression for $\hat{\lambda}_i$ given the
kernel parameters. The remaining problem — the minimisation of the profile
score $S(\theta_{j\to i})$ — is handed to the C99 Nelder–Mead simplex in
[src/calibration/hawkes_model/](src/calibration/hawkes_model/). This
reduction removes an ill-conditioned direction of the likelihood surface,
limits the number of local minima and drastically lowers the
per-iteration cost.

### 1.3 Recursive intensity: an FPGA-friendly formulation

A naive evaluation of $\lambda_i^*(t)$ at each new event is $O(k_i)$ in
memory scans; the exponential kernel avoids this. Introducing

$$
A_{i,j}(p) \;=\; \sum_{l : t_l^{(j)} < t_p^{(i)}} \exp\!\bigl(-\beta_{j,i}\,(t_p^{(i)} - t_l^{(j)})\bigr),
$$

the intensity admits the compact recursion

$$
\lambda_i^*(t_{p+1}^{(i)}) \;=\; \lambda_i \;+\; \sum_{j=1}^{m} \alpha_{j,i}\, A_{i,j}(p+1),
$$

where $A_{i,j}$ satisfies the telescoping identity

$$
A_{i,j}(p+1) \;=\; e^{-\beta_{j,i}\,(t_{p+1}^{(i)} - t_p^{(i)})}\, A_{i,j}(p)
\;+\; \sum_{l\in\mathcal{I}(p)} e^{-\beta_{j,i}\,(t_{p+1}^{(i)} - t_l^{(j)})}.
$$

A globally-indexed variant $\phi_{i,j}(t_k)$ is shown to be equivalent
to $A_{i,j}(p)$ and is the one implemented in
[HawkesModel](src/hawkesWorker.h). The update is $O(m)$ per event and
requires no history rescan — this is the *hot path* of the platform, and
its purely local, recursive structure is what makes the algorithm
amenable to future hardware acceleration (FPGA / SIMD) (see the work of C. Guo & W. Luk). 

### 1.4 Online residual analysis

Model quality is assessed *online* through the **compensator**

$$
\Lambda_i(t) \;=\; \int_0^t \lambda_i^*(s)\, ds.
$$

With the exponential kernel, $\Lambda_i$ inherits an incremental form
using the same $\phi_{i,j}$ already maintained by the intensity update:

$$
\Lambda_i(t_{k+1}) \;=\; \Lambda_i(t_k)
\;+\; \lambda_i\, \Delta t_k
\;+\; \sum_{j=1}^{m} \frac{\alpha_{j,i}}{\beta_{j,i}}
       \bigl(1 - e^{-\beta_{j,i}\, \Delta t_k}\bigr)\, \phi_{i,j}(t_k).
$$

The **random time-change theorem** (Papangelou, 1972) then yields the
diagnostic used by the platform: under a correctly-specified model, the
transformed times
$\{\Lambda_i(t_p^{(i)})\}_{p=1,\dots,k_i}$ form a unit-rate Poisson
process. Deviations from this null hypothesis — visualised in the
QQ-plot panel of the terminal — signal a drift of the fitted parameters
and trigger a re-optimisation by the HPC worker.

---

## 2. Objectives

The project pursues the following goals:

1. **Empirical characterisation** of self- and cross-exchange excitation in
   the crypto-asset ecosystem, per symbol and per time-window.
2. **Real-time estimation** of Hawkes parameters through a distributed
   optimisation architecture that scales with the number of physical cores.
3. **Interactive visualisation** of intensity trajectories, branching
   matrices and residual diagnostics through a low-latency terminal
   inspired by professional trading platforms.

---

## 3. System Architecture

The system is decomposed into five loosely-coupled modules communicating
through **thread-safe queues** ([src/tools.h](src/tools.h)):

```
     ┌──────────────────┐
     │  Exchange WS x N │  (Binance, Coinbase, Kraken, OKX, Bybit)
     └────────┬─────────┘
              │  raw JSON
              ▼
     ┌──────────────────┐   normalized_data
     │  GenericWS layer │  ─────────────────┐
     └──────────────────┘                   │
                                            ▼
                                 ┌────────────────────┐
                                 │  Shared Queue Q1   │
                                 └─────────┬──────────┘
                                           │
              ┌────────────────────────────┼─────────────────────────────┐
              │                            │                             │
              ▼                            ▼                             ▼
   ┌─────────────────┐        ┌──────────────────────┐       ┌──────────────────┐
   │ CalibrationEng. │        │ Worker pool (HPC)    │       │ UserInterface    │
   │ - assets volume │        │ - HawkesModel x N    │       │ - ImGui/ImPlot   │
   │ - bin-packing   │        │ - real-time updates  │       │ - live plots     │
   └─────────────────┘        └──────────┬───────────┘       │ - QQ / branching │
                                         │                   └──────────────────┘
                                         ▼
                              ┌──────────────────────┐
                              │  History → HPC opt   │
                              │  Nelder–Mead (C99)   │
                              │  Ogata thinning      │
                              └──────────────────────┘
```

### 3.1 The Scheduler — [src/scheduler.h](src/scheduler.h)

The `Scheduler` is the central coordinator. It initialises the system
in three states — `CALIBRATING`, `TRAINING`, then steady-state training —
and is responsible for **load balancing** the assets across workers via a
*greedy bin-packing* heuristic based on the per-symbol traffic volume
observed during the calibration window.

### 3.2 Exchange Connectivity — [src/genericWS.h](src/genericWS.h)

`GenericWebSocket` is an abstract base class that encapsulates connection
management, subscription payloads and reconnection logic on top of
[ixwebsocket](https://github.com/machinezone/IXWebSocket). Each exchange
provides a concrete subclass (`BinanceWS`, `CoinbaseWS`, `KrakenWS`,
`OkxWS`, `BybitWS`) that specialises the `normalise_message()` pure virtual
method, mapping venue-specific JSON payloads into the common
`normalized_data` struct declared in [src/struct.h](src/struct.h).

The subscription protocols, endpoint URLs and geolocation metadata for each
venue are declared in [config.json](config.json).

### 3.3 Calibration Engine — [src/calibration/calibration.h](src/calibration/calibration.h)

During the calibration phase, the engine samples the incoming stream over a
fixed window and produces a vector of per-symbol data volumes. This vector
is passed to `Scheduler::greedy_cores_packing()`, yielding a mapping between
symbols and worker identifiers that balances the expected computational
load across cores.

### 3.4 Hawkes Workers — [src/hawkesWorker.h](src/hawkesWorker.h)

Each `Worker` owns a collection of `HawkesModel` instances (one per symbol).
A `HawkesModel` maintains, at all times:

- the vector of current conditional intensities $\lambda_d(t)$;
- the per-exchange auxiliary vector $\phi_d$ used by the recursive
  intensity update — an FPGA-friendly formulation that avoids re-scanning
  the event history;
- the branching matrix estimate;
- the exponentially-weighted running residuals used for online model
  diagnostics.

### 3.5 HPC Optimiser — [src/calibration/hawkes_model/](src/calibration/hawkes_model/)

The parameter estimation kernel is written in **pure C99** for portability
and speed. It implements:

- a **Nelder–Mead simplex** optimiser
  ([optimization.c](src/calibration/hawkes_model/src/optimization.c))
  operating on the log-likelihood of the multivariate Hawkes process;
- **Ogata's thinning algorithm**
  ([ogata_thinning_algo.c](src/calibration/hawkes_model/src/ogata_thinning_algo.c))
  for exact simulation and validation of the fitted model.

The C kernel is linked into the C++ application through an `extern "C"`
interface declared in
[optimization.h](src/calibration/hawkes_model/include/optimization.h).

Optimised parameters are periodically persisted to disk as binary blobs
(`optimized_params_<SYMBOL>.bin`) so that the model can be warm-started on
subsequent runs.

### 3.6 Telemetry — [src/tools.h](src/tools.h)

The `TelemetryManager` is a lock-safe, per-symbol snapshot store used to
decouple producers (workers) from the consumer (UI). It relies on
`std::shared_mutex` to grant many concurrent readers or a single writer per
symbol, ensuring the render loop is never blocked by parameter updates.

### 3.7 User Interface — [src/frontend/user_interface.h](src/frontend/user_interface.h)

The graphical layer is built on **GLFW + OpenGL 3 + Dear ImGui + ImPlot**
and follows a *Bloomberg-like* aesthetic. It offers:

- a real-time scrolling plot of intensity trajectories $\lambda_d(t)$;
- a per-symbol heat-map view of the branching matrix $\Gamma_{d,d'}$;
- a residual/QQ-plot diagnostic panel;
- a control panel exposing calibration and training windows, worker
  budget, and symbol selection.

---

## 4. Concurrency Model

- Communication between modules relies exclusively on
  `ThreadSafeQueue<T>`, a bounded producer/consumer queue built on
  `std::mutex` and `std::condition_variable`.
- Read/write access to shared telemetry is arbitrated by
  `std::shared_mutex`, providing lock-free readers under contention.
- On Apple Silicon, worker threads are pinned to *Performance cores* via
  the `QOS_CLASS_USER_INITIATED` QoS class
  ([tools.h](src/tools.h)).
- A safety margin of two cores is reserved for the operating system, the
  UI thread and the WebSocket I/O layer.

---

## 5. Toolchain and Dependencies

| Layer            | Tool / Library                           |
|------------------|------------------------------------------|
| Build system     | CMake ≥ 3.10, vcpkg                      |
| Language         | C++17 (application) + C99 (HPC kernel)   |
| Networking       | [ixwebsocket](https://github.com/machinezone/IXWebSocket)         |
| JSON parsing     | JsonCpp                                  |
| Formatting       | fmt                                      |
| Windowing / GL   | GLFW3, OpenGL                            |
| GUI              | [Dear ImGui](https://github.com/ocornut/imgui) |
| Plotting         | [ImPlot](https://github.com/epezent/implot)     |
| Analysis         | Python 3 (Jupyter, see [resultAnalyser.ipynb](resultAnalyser.ipynb) and [python/](python/)) |

Dependencies fetched by vcpkg are declared in [vcpkg.json](vcpkg.json).

---

## 6. Building and Running

### Build

```sh
# Bootstrap dependencies via vcpkg
vcpkg install

# Create the build folder
mkdir build
cd build

# Configure
cmake --preset default
```

### Launch the stress-tests
```sh
# build
cmake -DSTRESS_TEST=ON .. 
cmake --build . --target MonApp

# Launch
./MonApp [n_dimensions] [time_execution] [n_symbols] [simulation_speed]
```

Example : 
```./MonApp 5 60 10 100```

### Launch on real data
```sh
# build
cmake -DSTRESS_TEST=ON .. 
cmake --build . --target MonApp

# Launch
./MonApp
```

The application will attempt to open WebSocket connections to the five
supported exchanges. Ensure that outbound TCP/TLS traffic is not blocked
by a firewall before launch.

---

## 7. Repository Layout

```
AlgoHawkes/
├── CMakeLists.txt              Build definition
├── config.json                 Per-exchange WebSocket protocols
├── src/
│   ├── main.cpp                Application entry point
│   ├── scheduler.[h|cpp]       Central coordination and load balancing
│   ├── genericWS.[h|cpp]       WebSocket abstraction + venue implementations
│   ├── hawkesWorker.[h|cpp]    Online Hawkes model and worker pool
│   ├── struct.h                Common data structures
│   ├── tools.h                 Thread-safe queue + telemetry manager
│   ├── calibration/
│   │   ├── calibration.[h|cpp]         Volume-based load balancing
│   │   └── hawkes_model/               C99 optimiser (Nelder-Mead, Ogata)
│   ├── frontend/                       ImGui/ImPlot terminal
│   └── saves/                          Persisted optimised parameters
├── python/                     Reference Python prototypes
├── imgui/, implot/             Vendored GUI libraries
└── include/                    Third-party headers
```

---

## 8. Academic Positioning and Future Work

AlgoHawkes is a **research artefact**, not a production trading system. Its
public release is motivated by the belief that reproducible, open
implementations accelerate empirical work in market microstructure. Planned
extensions include:

- non-exponential kernels (power-law, sum-of-exponentials);
- non-parametric estimation via the Bacry–Muzy Wiener–Hopf approach;
- integration of alternative models (LSTM, Transformer) already reserved in
  the UI selector, for comparative benchmarking against the Hawkes baseline.

Contributions, replications and critiques are welcome.

---

## 9. References

- V. Filimonov & D. Sornette, *Apparent criticality and calibration issues
  in the Hawkes self-excited point process model: application to
  high-frequency financial data*, Quantitative Finance 15 (2015),
  pp. 1293–1314.
- P. Embrechts, T. Liniger & L. Lin, *Multivariate Hawkes processes: an
  application to financial data*, Journal of Applied Probability 48 (A)
  (2011), pp. 367–378.
- A. A. Lyubushin & V. F. Pisarenko, *Research on seismic regime using a
  linear model of intensity of interacting point processes*, Izvestiya,
  Physics of the Solid Earth 29 (1994), pp. 1108–1113.
- P. J. Laub, T. Taimre & P. K. Pollett, *Hawkes processes*, arXiv preprint
  arXiv:1507.02822 (2015).
- F. Papangelou, *Integrability of expected increments of point processes
  and a related random change of scale*, Transactions of the American
  Mathematical Society 165 (1972), pp. 483–506.
- Guo, C., & Luk, W. (2013, September). Accelerating maximum likelihood estimation for hawkes point processes. In 2013 23rd     International Conference on Field programmable Logic and Applications (pp. 1-6). IEEE.

---

## License

Released for academic and research purposes. Please cite this repository if
the code contributes to a scholarly publication.
