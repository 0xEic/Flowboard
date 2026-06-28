// SPDX-License-Identifier: MIT
//
// Per-node-type help content shown in the in-app Help panel.
//
// Each entry is keyed by the engine type name (e.g. "Transform.Convert"). The
// Help panel reads NodePalette's PALETTE_LAYOUT to render category sections and
// uses entries by `type` to populate each section. Anchor IDs are derived from
// the type ("Transform.Convert" → "help-Transform.Convert") and consumed by the
// help-icon click handler so opening a node's help scrolls straight to it.
//
// OnboardAPI's M_<X>.Service / M_<X>.Client / Factory.* are generated and
// share a single "OnboardAPI" entry — there are hundreds of them, so the panel
// renders them under one umbrella section with a short explanation.

export type NodeHelp = {
  type: string;
  category: string;
  title: string;          // Human-readable label
  summary: string;        // One-line elevator pitch
  description?: string;   // Longer prose (Markdown-lite — newlines preserved)
  inputs?: string;        // Port list: "name:Type — note, ..."
  outputs?: string;
  config?: string;        // Config keys: "key — note, ..."
  example?: string;       // Short usage hint
};

export const NODE_HELP: NodeHelp[] = [
  // ─────────────────────────────────────────── Debug ─────────────────────────
  {
    type: 'Debug.ValueDisplay',
    category: 'Debug',
    title: 'Value Display',
    summary: 'Shows the live value flowing into its input on the canvas.',
    description:
      'A read-only canvas widget that prints whatever value arrives on `in`. Set `inputType` to match the upstream port. For struct or list inputs, list one or more dot-paths under `fields` to extract and display sub-values (e.g. `Position.X`). Numbers are formatted with up to 3 decimals; everything else is JSON-stringified. The node is a pure sink and does not emit anything.',
    inputs: '`in` : the value to display (any registered primitive or struct type)',
    outputs: '— (sink, no outputs)',
    config:
      '`inputType` — type-tag of `in` (default `flowboard::Double`); `fields` — optional dot-paths shown as rows when the value is an object or list (empty = show the whole value)',
    example: 'Sources.Iterator → Debug.ValueDisplay (inputType Double) to watch the current step value on the canvas.',
  },
  {
    type: 'Debug.GraphDisplay',
    category: 'Debug',
    title: 'Graph Display',
    summary: 'Plots a numeric input over time as a sample-and-hold line chart.',
    description:
      'Accepts any numeric or boolean type (Bool is coerced to 0/1) and renders a step-line chart on the canvas. Samples are captured on the node thread so no values are lost when the browser tab is in the background. A rolling `timeWindowSec` keeps only recent history. Y-axis can auto-scale to the data or be fixed with `yMin`/`yMax`.',
    inputs: '`in` : numeric or Bool value to plot (Double, Float, Int64, UInt64, Int32, UInt32, Int16, UInt16, UInt8, or Bool)',
    outputs: '—',
    config:
      '`inputType` (default `flowboard::Double`); `timeWindowSec` — rolling window size in seconds (default 30); `autoScale` — fit Y to data (default true); `yMin` / `yMax` — fixed Y range when autoScale is off',
    example: 'Sources.Iterator `value` → Debug.GraphDisplay to plot a rising ramp over time.',
  },
  {
    type: 'Debug.TriggerButton',
    category: 'Debug',
    title: 'Trigger Button',
    summary: 'A canvas button that emits a Bool on click or while held.',
    description:
      'Renders a labelled button on the canvas. In `pushbutton` mode it emits `true` while the button is held and `false` on release. In `switch` mode each click toggles the state and emits the new value. Useful to manually drive Cmd ports or to gate downstream nodes from the UI without wiring a separate source.',
    inputs: '—',
    outputs: '`out` : Bool',
    config: '`label` — caption shown on the canvas button (default "Trigger"); `mode` — `pushbutton` or `switch` (default `pushbutton`)',
    example: 'Debug.TriggerButton (mode switch) → Sinks.Log to start and stop logging on demand.',
  },

  // ─────────────────────────────────────────── Sources ───────────────────────
  {
    type: 'Sources.CurrentTime',
    category: 'Sources',
    title: 'Current Time',
    summary: 'Emits the host system wall-clock time on each trigger pulse.',
    description:
      'Samples the system clock whenever `trigger` receives a true value and emits the result on up to three output ports simultaneously. The `unit` config selects whether `epoch` carries milliseconds or seconds since the Unix epoch, or whether `iso` carries an ISO-8601 UTC string; `commonTime` (M_Common::TimeType) always emits regardless of `unit`. Enable `autoTrigger` to self-fire every `autoTriggerMs` milliseconds without an external trigger.',
    inputs: '`trigger` : Bool — rising edge (true) samples the clock',
    outputs:
      '`epoch` : Int64 — ms or s since Unix epoch (when unit is epoch_ms or epoch_s); `iso` : String — UTC ISO-8601 (when unit is iso8601); `commonTime` : M_Common::TimeType — always emitted',
    config:
      '`unit` — `epoch_ms` / `epoch_s` / `iso8601` (default `epoch_ms`); `autoTrigger` — self-fire without an external pulse (default false); `autoTriggerMs` — self-fire interval in ms (default 1000)',
    example: 'Sources.CurrentTime (autoTrigger true, autoTriggerMs 5000) `commonTime` → Sinks.Log to log a timestamp every 5 seconds.',
  },
  {
    type: 'Sources.TimeSource',
    category: 'Sources',
    title: 'Time Source',
    summary: 'A simulated clock that advances from a configured date/time anchor.',
    description:
      'Emits an advancing time stream starting from the date/time set in config (year, month, day, hour, minute, second, millisecond). While the `running` input is true, the clock advances in real time and emits all three representations every `updateMs` milliseconds. When `running` goes false the clock freezes at its current value. Useful for replaying logs or driving timestamps in test graphs without touching the system clock.',
    inputs: '`running` : Bool — true to advance the clock, false to freeze it',
    outputs:
      '`epoch` : Int64 — ms since Unix epoch; `iso` : String — UTC ISO-8601; `commonTime` : M_Common::TimeType',
    config:
      '`year` / `month` / `day` / `hour` / `minute` / `second` / `millisecond` — UTC start anchor (default 2026-01-01 00:00:00.000); `updateMs` — emit cadence while running (default 1000)',
    example: 'Debug.TriggerButton (switch) → Sources.TimeSource `running`; TimeSource `epoch` → Debug.GraphDisplay to watch simulated time advance.',
  },
  {
    type: 'Sources.Timer',
    category: 'Sources',
    title: 'Timer',
    summary: 'Pulses `tick` every configured interval; tracks elapsed and remaining time.',
    description:
      'A repeating or one-shot interval timer. By default it starts as soon as the graph loads (`autostart` true) and fires `tick` = true every `intervalMs` milliseconds. Set `repeat` to false for a one-shot that stops after the first tick. `start` arms or resumes a stopped timer; `reset` restarts the current interval without stopping. Progress outputs `timePast` and `timeLeft` update at up to 100 ms cadence so progress bars stay smooth.',
    inputs: '`start` : Bool — true arms or resumes the timer; `reset` : Bool — true restarts the current interval',
    outputs:
      '`tick` : Bool — emits true at each interval boundary; `isRunning` : Bool — emitted on state change; `timePast` : Int64 — ms elapsed in the current interval; `timeLeft` : Int64 — ms remaining',
    config: '`intervalMs` — period between ticks in ms (default 1000); `repeat` — keep firing after each tick (default true); `autostart` — begin running at graph start (default true)',
    example: 'Sources.Timer (intervalMs 500) `tick` → Sources.Iterator `start` to step an iterator twice per second.',
  },
  {
    type: 'Sources.Iterator',
    category: 'Sources',
    title: 'Iterator',
    summary: 'Sweeps a numeric value from a start to an end in configurable steps.',
    description:
      'A numeric ramp generator that steps from `startValue` to `endValue` in increments of `stepSize`, waiting `timeBetweenStepsMs` between each step. Steps are emitted on the `value` output. `autoReverse` makes the sweep bounce back to the start (ping-pong); `loop` repeats the sweep indefinitely. Per-direction easing curves (linear, quad, cubic, sine, expo, etc.) can reshape the sweep near either end. All numeric config keys are also available as live input ports for runtime control. The `valueType` config selects the numeric type (Double by default).',
    inputs:
      '`start` / `stop` / `pause` / `reset` : Bool triggers; `startValue` / `endValue` / `stepSize` : <valueType> runtime overrides; `timeBetweenStepsMs` : Int64 override; `loop` / `autoReverse` : Bool overrides',
    outputs:
      '`value` : <valueType> — current swept value; `running` : Bool; `started` : Bool; `endReached` : Bool; `increasing` : Bool; `decreasing` : Bool',
    config:
      '`valueType` — numeric type-tag (default `flowboard::Double`); `startValue` / `endValue` / `stepSize` / `timeBetweenStepsMs` — initial values; `loop`; `autoReverse`; `autostart`; `specialStartStartValue` / `specialStartEndValue` / `specialEndStartValue` / `specialEndEndValue` — easing curves per direction and end',
    example: 'Sources.Iterator (startValue 0, endValue 100, stepSize 1, timeBetweenStepsMs 50, loop true) `value` → Debug.GraphDisplay to watch a looping ramp.',
  },
  {
    type: 'Transform.ConstantSource',
    category: 'Sources',
    title: 'Constant Source',
    summary: 'Emits a fixed primitive value of any type on each trigger.',
    description:
      'Stores a single typed constant set in the inspector and re-emits it every time `trigger` receives true. With `autoTriggerOnInit` enabled (the default) it also emits once when the graph starts, making it useful to seed downstream nodes without wiring a separate trigger. Supports all primitive types including Bool, all integer widths, Float, Double, String, and Char.',
    inputs: '`trigger` : Bool — true causes the constant to be emitted',
    outputs: '`out` : <outputType> — the configured constant value',
    config: '`outputType` — type-tag (default `flowboard::String`); `value` — the constant to emit; `autoTriggerOnInit` — emit once at graph start (default true)',
    example: 'Transform.ConstantSource (outputType Double, value 3.14, autoTriggerOnInit true) `out` → Manipulate.Multiply to inject a fixed factor at startup.',
  },
  {
    type: 'Sources.Random',
    category: 'Sources',
    title: 'Random',
    summary: 'Emits a fresh random value of any primitive type on each trigger.',
    description:
      'Generates one random value per true pulse on `trigger` using a Mersenne-Twister RNG. For numeric types, `min`/`max` bound the uniform range (defaults 0–100). For Bool, `trueProbability` sets the bias (default 0.5). For String and Char, `length`, `allowLowercase`, `allowUppercase`, `allowDigits`, and `allowSymbols` control the alphabet and character count. With `autoTriggerOnInit` (default true) an initial value is emitted when the graph starts.',
    inputs: '`trigger` : Bool — each true pulse produces one random value',
    outputs: '`out` : <outputType>',
    config:
      '`outputType` (default `flowboard::Double`); `min` / `max` — numeric range (default 0/100); `trueProbability` — Bool bias (default 0.5); `length` — String character count (default 8); `allowLowercase` / `allowUppercase` / `allowDigits` / `allowSymbols` — String/Char alphabet flags; `autoTriggerOnInit` — emit on graph start (default true)',
    example: 'Sources.Timer `tick` → Sources.Random (outputType Double, min 0, max 1) `out` → Debug.GraphDisplay to watch noise every second.',
  },

  // ─────────────────────────────────────────── Transform ─────────────────────
  {
    type: 'Transform.Convert',
    category: 'Transform',
    title: 'Convert',
    summary: 'Converts a value between primitive types using one of six transform modes.',
    description:
      'A multi-mode typed converter. `convert` casts between any two primitive types; `scale` applies `out = in * scale + offset` with optional clamping; `lookup` maps integer inputs through a key/value table with a `fallback`; `threshold` turns a number into a Bool via a comparison operator; `boolmap` maps `true`/`false` to configured output values; `case` applies `upper`, `lower`, or `trim` to strings. Set `inputType` and `outputType` to any supported primitive; modes that do not apply to the selected types are ignored.',
    inputs: '`in` : <inputType>',
    outputs: '`out` : <outputType>',
    config:
      '`inputType`, `outputType`, `mode` (convert/scale/lookup/threshold/boolmap/case), `scale`, `offset`, `clamp`, `clampMin`/`clampMax`, `template`, `decimals`, `mappings`, `fallback`, `threshold`, `compareOp`, `trueValue`/`falseValue`, `textTransform`',
    example: 'Sensor.rpm → Transform.Convert (mode scale, scale 0.10472, inputType Double, outputType Double) → Display.Gauge — converts RPM to rad/s.',
  },
  {
    type: 'Transform.Extract',
    category: 'Transform',
    title: 'Extract',
    summary: 'Pulls a single field out of a struct value.',
    description:
      'Configure the struct `inputType` and pick a `field`; the node emits that field on `value` and an `isFilled` flag when the struct provides a value. Dot-paths drill into nested structs. Use this to break apart composite messages — for example pulling `Elevation` out of a mount position struct before feeding it into a Threshold node.',
    inputs: '`in` : <inputType>',
    outputs: '`value` : <fieldType> — the extracted field; `isFilled` : Bool — true when a value was present',
    config: '`inputType`, `field` (dot-path supported)',
    example: 'Mount.Position → Transform.Extract (field Elevation) → Transform.Threshold.',
  },
  {
    type: 'Transform.Inverter',
    category: 'Transform',
    title: 'Inverter',
    summary: 'Boolean NOT — flips true to false and false to true.',
    description:
      'Emits the logical negation of every Bool received on `in`. Use it to reverse a condition before feeding it into a Filter gate or a logic combinator.',
    inputs: '`in` : Bool',
    outputs: '`out` : Bool — the logical negation of the input',
    example: 'Transform.Threshold → Transform.Inverter → Transform.Filter (gate).',
  },
  {
    type: 'Transform.Pulse',
    category: 'Transform',
    title: 'Pulse',
    summary: 'Emits a single `true` pulse on every edge of a boolean signal.',
    description:
      'Edge detector: fires a `true` on `out` whenever the value on `in` changes (both rising and falling edges). The first value received is always treated as a change. Repeated identical values are suppressed. Use it to convert a level signal — such as a button that holds `true` while pressed — into a momentary trigger that fires on press and again on release.',
    inputs: '`in` : Bool',
    outputs: '`out` : Bool — always `true`, emitted once per edge',
    example: 'Input.ButtonHandler → Transform.Pulse → Trigger.ServiceRequest.',
  },
  {
    type: 'Transform.Derivative',
    category: 'Transform',
    title: 'Derivative',
    summary: 'Emits the rate of change (per second) of a numeric input.',
    description:
      'Computes d(value)/dt from successive samples using wall-clock arrival times: `out = (value_now - value_prev) / seconds_elapsed`. The first sample primes the state without producing output; samples with a non-positive time delta are skipped. The output is always `Double` regardless of the input type, representing input units per second. Useful for turning a position stream into a velocity stream.',
    inputs: '`in` : Number (numeric primitives only — Bool and String are not supported)',
    outputs: '`out` : Double — rate of change in input units per second',
    config: '`inputType` — numeric primitive type tag (default `flowboard::Double`)',
    example: 'Mount.Azimuth → Transform.Derivative → Display.Rate.',
  },
  {
    type: 'Transform.Arithmetic',
    category: 'Transform',
    title: 'Arithmetic',
    summary: 'Two-input math: add, subtract, multiply, divide, min, or max.',
    description:
      'Latches the last value from both `a` and `b`, then emits `op(a, b)` on every update once both inputs have been seen. Integer division by zero is silently skipped with a warning; floating-point division by zero produces the platform inf/nan. All ports share the same numeric type.',
    inputs: '`a` : Number; `b` : Number',
    outputs: '`out` : Number — result of op(a, b)',
    config: '`op` — `add`/`subtract`/`multiply`/`divide`/`min`/`max`; `inputType`',
    example: 'Sensor.RawValue → Transform.Arithmetic (op subtract) ← Sensor.Offset; → Display.Value.',
  },
  {
    type: 'Transform.NumberHolder',
    category: 'Transform',
    title: 'Number Holder',
    summary: 'A latch you can set, increment, and decrement with pulse inputs.',
    description:
      'Holds a single numeric value of the configured type. `initValue` sets (or resets) the stored value to the received amount; `increase` adds the delta; `decrease` subtracts it. The current value is emitted on every change. When `emitOnInit` is true, the initial value is also emitted once when the graph starts. Use it to implement a counter, an adjustable offset, or a stepped setpoint driven by button events.',
    inputs: '`increase` : Number — delta to add; `decrease` : Number — delta to subtract; `initValue` : Number — sets the held value',
    outputs: '`value` : Number — the current held value, emitted on every change',
    config: '`valueType`, `initValue` (starting value, default 0), `emitOnInit` (default true)',
    example: 'ButtonUp → Transform.NumberHolder (increase 1) ← ButtonDown (decrease 1); value → Display.Counter.',
  },
  {
    type: 'Manipulate.Number',
    category: 'Transform',
    title: 'Manipulate Number',
    summary: 'Evaluates a numeric formula on the input.',
    description:
      'Apply a math expression to `in` (referenced as `x`). Supports arithmetic, comparison-as-0/1, and common functions. Useful for unit conversions or quick remapping.',
    inputs: '`in` : Number',
    outputs: '`out` : Number',
    config: '`formula` (uses `x` as the input); `valueType`',
    example: '`formula: x * 1.8 + 32` converts Celsius to Fahrenheit.',
  },
  {
    type: 'Manipulate.String',
    category: 'Transform',
    title: 'Manipulate String',
    summary: 'Build, replace, append, or prepend text on a string value.',
    description:
      '`replace` mode finds a literal snippet inside the incoming string and replaces it with another. `set` discards the input and emits the configured `text` unchanged. `prepend` and `append` extend the incoming string with `text` at the front or back. All modes emit a String on `out` for every value received on `in`.',
    inputs: '`in` : String',
    outputs: '`out` : String — the transformed string',
    config:
      '`mode` — `replace` / `set` / `prepend` / `append`; `find` and `replace` (replace mode); `text` (the literal used by set/prepend/append)',
    example: 'Sensor.Label → Manipulate.String (mode prepend, text "Status: ") → Display.Text.',
  },
  {
    type: 'Transform.Compare',
    category: 'Logic & Conditions',
    title: 'Compare',
    summary: 'Boolean output from a two-input comparison of numbers or strings.',
    description:
      'Latches the last value from `a` and `b` and emits the boolean result of `a op b` whenever either input updates (after both have a value). Numeric types support `>`, `>=`, `<`, `<=`, `==`, `!=`; String supports `equals`, `notEquals`, `contains`, `startsWith`, `endsWith`. Use this instead of Threshold when the right-hand side is itself a live signal.',
    inputs: '`a` : Number or String; `b` : Number or String (same type as `a`)',
    outputs: '`out` : Bool — result of a op b',
    config: '`inputType`; `op` — `>`/`>=`/`<`/`<=`/`==`/`!=`/`equals`/`notEquals`/`contains`/`startsWith`/`endsWith`',
    example: 'Sensor.Temperature → Transform.Compare (op >) ← Setpoint.Temperature; out → Alarm.Trigger.',
  },
  {
    type: 'Transform.Threshold',
    category: 'Logic & Conditions',
    title: 'Threshold',
    summary: 'Compares `in` against a fixed constant and emits a boolean.',
    description:
      'On every input value emits `in op value` as a Bool, where `value` is a constant configured in the inspector. Supports all primitive types including String and Bool. Unlike Transform.Compare, the right-hand side is static — ideal for alarm limits, zero-crossing detection, or converting a sensor reading into an on/off signal.',
    inputs: '`in` : <inputType>',
    outputs: '`out` : Bool — true when `in op value` holds',
    config: '`inputType`, `op` — `>`/`>=`/`<`/`<=`/`==`/`!=`; `value` — the constant threshold',
    example: 'Sensor.Speed → Transform.Threshold (op >=, value 100) → Alarm.Overspeed.',
  },
  {
    type: 'Transform.Filter',
    category: 'Logic & Conditions',
    title: 'Filter',
    summary: 'Passes `in` to `out` only while `gate` is true.',
    description:
      'When `gate` is true the node forwards every `in` value immediately. When `gate` turns false, it either holds the last emitted value (`closedBehavior hold`, the default) or emits a configured `defaultValue` (`closedBehavior default`). Opening the gate re-emits the current `in` so `out` catches up without waiting for the next `in` event.',
    inputs: '`gate` : Bool; `in` : <inputType>',
    outputs: '`out` : <inputType> — forwarded values while gate is open',
    config: '`inputType`, `closedBehavior` (hold/default), `defaultValue`',
    example: 'Sensor.Value → Transform.Filter (gate) ← Safety.EnabledFlag; out → Display.Gauge.',
  },

  // ─────────────────────────────────────────── Lists ─────────────────────────
  {
    type: 'Transform.List.Build',
    category: 'Lists',
    title: 'List · Build',
    summary: 'Assembles N typed item inputs into a single list when `trigger` fires.',
    description:
      'Exposes `item0`..`itemN-1` input ports (count set by `count`) and bundles whatever values are present into a `List<elementType>` when `trigger` pulses true. Items not yet received are skipped. Supports all primitive types; struct element types plug in via the codegen registry. Use this when you know the list length at design time.',
    inputs: '`item0`..`itemN-1` : <elementType>; `trigger` : Bool',
    outputs: '`out` : List<elementType> — emitted on each trigger pulse',
    config: '`elementType` (type tag, default flowboard::Double), `count` (0–64, default 2)',
    example: 'Sensor.A → List.Build.item0; Sensor.B → List.Build.item1; Clock.Tick → List.Build.trigger → List.Sort.',
  },
  {
    type: 'Transform.List.Constant',
    category: 'Lists',
    title: 'List · Constant',
    summary: 'Emits a fixed, author-defined list on start and on each trigger pulse.',
    description:
      'The list contents are authored as a JSON array in `values`; `elementType` records the element type tag for downstream checks. When `autoTriggerOnInit` is true (default) the list is emitted once on pipeline start without needing a trigger. Re-emits on any subsequent `trigger` pulse. Use for lookup tables or static configuration lists.',
    inputs: '`trigger` : Bool',
    outputs: '`out` : List<elementType>',
    config: '`elementType` (type tag), `values` (JSON array of elements), `autoTriggerOnInit` (Bool, default true)',
    example: 'List.Constant (values [10,20,30], autoTriggerOnInit true) → List.Filter (field "", op gt, value 15).',
  },
  {
    type: 'Transform.List.Accumulate',
    category: 'Lists',
    title: 'List · Accumulate',
    summary: 'Grows a list by appending each arriving item; emits the running list on trigger.',
    description:
      'Each value arriving on `item` is pushed onto an internal buffer. When `trigger` pulses true the current buffer is emitted as a `List<elementType>`. A pulse on `reset` clears the buffer. `maxItems` caps the buffer size using FIFO eviction (0 = unbounded). Supports all primitive types and struct types via the codegen registry.',
    inputs: '`item` : <elementType>; `trigger` : Bool; `reset` : Bool',
    outputs: '`out` : List<elementType> — emitted on each trigger pulse',
    config: '`elementType` (type tag, default flowboard::Double), `maxItems` (Int, 0 = unbounded)',
    example: 'Sensor.Value → List.Accumulate (elementType flowboard::Double, maxItems 100); Clock.1Hz → List.Accumulate.trigger → List.Sort.',
  },
  {
    type: 'Transform.List.Combine',
    category: 'Lists',
    title: 'List · Combine',
    summary: 'Concatenates two lists (a then b) into one output list.',
    description:
      'Re-emits on every update to either `a` or `b`, using the most recently seen value for the other input. The output element type tag is taken from `a`; if `a` is absent it falls back to `b`. Both inputs should carry the same element type — the Inspector flags a soft mismatch otherwise. No config keys.',
    inputs: '`a` : List<T>; `b` : List<T>',
    outputs: '`out` : List<T> — elements of `a` followed by elements of `b`',
    example: 'List.Constant (evens) → Combine.a; List.Accumulate → Combine.b → List.Sort.',
  },
  {
    type: 'Transform.List.Filter',
    category: 'Lists',
    title: 'List · Filter',
    summary: 'Keeps only list items whose field passes a predicate test.',
    description:
      'For each element in the incoming list, resolves the dot-path `field` and evaluates it against `value` using `op`. Elements where the predicate is true are kept; others are dropped. An empty `field` tests the element itself. String comparison is lexicographic; `contains` requires both sides to be strings. Emits on every input.',
    inputs: '`in` : List',
    outputs: '`out` : List — only items satisfying the predicate',
    config: '`field` (dot-path, empty = whole element), `op` (eq/neq/lt/lte/gt/gte/contains), `value` (JSON)',
    example: 'List.Accumulate → List.Filter (field "severity", op gte, value 2) → List.Size.',
  },
  {
    type: 'Transform.List.Find',
    category: 'Lists',
    title: 'List · Find',
    summary: 'Returns the first list item that matches a predicate, plus an `isFound` flag.',
    description:
      'Scans the list in order and stops at the first element where the dot-path `field` satisfies the `op`/`value` predicate. That element is wrapped in a single-element `List` on `value`; `isFound` is true. When no element matches, `value` is an empty list and `isFound` is false. Uses the same predicate model as List.Filter.',
    inputs: '`in` : List',
    outputs: '`value` : List (0 or 1 element); `isFound` : Bool',
    config: '`field` (dot-path, empty = whole element), `op` (eq/neq/lt/lte/gt/gte/contains), `value` (JSON)',
    example: 'List.Constant (device records) → List.Find (field "id", op eq, value "cam-3") → Logic.If (isFound).',
  },
  {
    type: 'Transform.List.GetAt',
    category: 'Lists',
    title: 'List · GetAt',
    summary: 'Picks the element at a fixed index, with an `isPresent` guard.',
    description:
      'Reads the element at zero-based `index` from the incoming list. Negative indices count from the end (-1 = last element). The result is emitted as a single-element `List` on `value`; `isPresent` is false and `value` is empty when the index is out of range. Emits on every input.',
    inputs: '`in` : List',
    outputs: '`value` : List (0 or 1 element); `isPresent` : Bool',
    config: '`index` (Int64, default 0; negative counts from end)',
    example: 'List.Sort → List.GetAt (index 0) → isPresent guard → downstream display (head of sorted list).',
  },
  {
    type: 'Transform.List.MapField',
    category: 'Lists',
    title: 'List · MapField',
    summary: 'Applies a per-element transform to one field across the whole list.',
    description:
      'Resolves the dot-path `field` on every element and rewrites it in-place. When `valueType` is "number", the field is passed to the arithmetic `formula` as `x` (e.g. "x * 0.0174533" to convert degrees to radians). When `valueType` is "string", `mode` selects the operation (replace/set/prepend/append) using `find`, `replace`, and `text`. The list structure and element count are preserved.',
    inputs: '`in` : List',
    outputs: '`out` : List — same length, with the chosen field rewritten on each element',
    config: '`field` (dot-path, empty = element itself), `valueType` (number/string), `formula` (number mode), `mode`/`find`/`replace`/`text` (string mode)',
    example: 'List.Accumulate (angles) → List.MapField (field "value.Azimuth", valueType number, formula "x * 0.0174533") → List.Sort.',
  },
  {
    type: 'Transform.List.Size',
    category: 'Lists',
    title: 'List · Size',
    summary: 'Emits the element count and an `isEmpty` flag for a list.',
    description:
      'On every incoming list, emits the number of elements as `count` (Int64) and `isEmpty` (Bool). No config. Useful for gating downstream logic on whether a list has any items, or for feeding a display.',
    inputs: '`in` : List',
    outputs: '`count` : Int64; `isEmpty` : Bool',
    example: 'List.Filter → List.Size; isEmpty → Logic.If (branch on empty result); count → UI.Label.',
  },
  {
    type: 'Transform.List.Sort',
    category: 'Lists',
    title: 'List · Sort',
    summary: 'Sorts list elements by a field path, ascending or descending.',
    description:
      'Uses a stable sort over the incoming list, comparing each element by the value at dot-path `field`. An empty `field` compares whole elements. Numbers compare numerically and strings lexicographically. `ascending` (default true) controls direction. The sorted list is emitted on `out` on every input.',
    inputs: '`in` : List',
    outputs: '`out` : List — same elements in sorted order',
    config: '`field` (dot-path, empty = whole element), `ascending` (Bool, default true)',
    example: 'List.Accumulate (sensor readings) → List.Sort (field "timestamp", ascending false) → List.GetAt (index 0) → display most-recent.',
  },

  // ─────────────────────────────────────────── Timing & Sync ─────────────────
  {
    type: 'Transform.Delay',
    category: 'Timing & Sync',
    title: 'Delay',
    summary: 'Re-emits each value a fixed time after it arrives.',
    description:
      'Holds every value that arrives on `in` and re-emits it unchanged on `out` after `delayMs` milliseconds. Values are queued independently, so several arriving within one delay window each carry their own countdown and emerge in arrival order. Works for any registered type (primitive, list, or OnboardAPI struct). Pending values are dropped when the graph stops; the countdown freezes while paused.',
    inputs: '`in` : <inputType>',
    outputs: '`out` : <inputType> — same value, `delayMs` ms later',
    config:
      '`inputType` — type tag carried by `in`/`out`; `delayMs` — milliseconds to hold each value (≥ 0)',
    example: 'Sources.Iterator → Transform.Delay (delayMs 1000) → Debug.GraphDisplay to make the plot lag the source by one second.',
  },
  {
    type: 'Transform.Throttle',
    category: 'Timing & Sync',
    title: 'Throttle',
    summary: 'Limits how often a value can pass downstream.',
    description:
      'Drops updates that arrive sooner than `minPeriodMs` after the last forwarded value. Use it to cap the rate of a high-frequency sensor stream or UI update. Works for any registered primitive or OnboardAPI struct type.',
    inputs: '`in` : <inputType>',
    outputs: '`out` : <inputType> — rate-limited stream',
    config: '`inputType` — type tag for `in`/`out`; `minPeriodMs` — minimum gap between forwarded values (ms, ≥ 0, default 100)',
    example: 'Can.Bus.rx → Transform.Throttle (minPeriodMs 50) → Debug.GraphDisplay to display at most 20 frames/s.',
  },
  {
    type: 'Transform.Synchronize',
    category: 'Timing & Sync',
    title: 'Synchronize',
    summary: 'Waits until every input has a value, then emits all together.',
    description:
      'Each `inN` port buffers its latest value. Once every slot is populated, the node emits `beforeOutput` (true), then all `outN` values in the order given by `order`, then `afterOutput` (true). Subsequent arrivals update the buffer and trigger another emission. Each input/output pair can carry a different type via `inputTypes`; `inputCount` sets the number of pairs.',
    inputs: '`in0`..`inN-1` : per-pair type (see `inputTypes`)',
    outputs: '`out0`..`outN-1` : per-pair type; `beforeOutput` : Bool; `afterOutput` : Bool',
    config: '`inputCount` — number of in/out pairs; `inputType` — fallback type for all pairs; `inputTypes` — per-pair type tags array (overrides `inputType`); `order` — emission order of value outputs; `defaults` — per-input default values',
    example: 'Sensor.A.value + Sensor.B.value → Transform.Synchronize (inputCount 2) → Math.Add to align two asynchronous streams before arithmetic.',
  },
  {
    type: 'Transform.KeyValueAccumulator',
    category: 'Timing & Sync',
    title: 'Key/Value Accumulator',
    summary: 'Maintains a live key-to-value map and emits the current entries as a list.',
    description:
      'Set or update an entry by pulsing `key` and `value` together. Remove an entry by pulsing `key` with `isRemoved` = true. After each change the node emits the full current map as a `List` on `list`, ordered by insertion time or lexicographic key. Use it to track per-channel state or accumulate named readings without a fixed schema.',
    inputs: '`key` : <keyType>; `value` : <valueType>; `isRemoved` : Bool',
    outputs: '`list` : List<{key, value}> — full current map after every change',
    config: '`keyType`, `valueType` — type tags for key and value ports; `elementTypeTag` — optional informational tag on the emitted list; `orderBy` — `insertion` or `key`; `emitOnEmpty` — re-emit when a remove targets an absent key (default true)',
    example: 'HID axis events → Transform.KeyValueAccumulator (keyType String, valueType Double) → Debug.GraphDisplay to track the latest value per axis name.',
  },

  // ─────────────────────────────────────────── Sinks ─────────────────────────
  {
    type: 'Sinks.Log',
    category: 'Sinks',
    title: 'Log',
    summary: 'Appends incoming values to a log file or the default logger.',
    description:
      'Each value received on `in` is serialised to a string and written as a line prefixed by `prefix`. When `path` is empty the line goes to the default spdlog sink (stdout). When `path` is set lines are appended to that file. Works for any registered primitive, list, or OnboardAPI struct type.',
    inputs: '`in` : <inputType>',
    outputs: '—',
    config: '`inputType` — type tag for the `in` port; `prefix` — text prepended to each log line (default `value`); `path` — file to append to, empty means stdout logger',
    example: 'Can.Bus.rx → Sinks.Log (prefix "frame", path "/tmp/can.log") to record every raw CAN frame.',
  },

  // ─────────────────────────────────────────── Input ─────────────────────────
  {
    type: 'Input.ButtonHandler',
    category: 'Input',
    title: 'Button Handler',
    summary: 'Fans out a HidJoystick EventButton event to per-button Bool outputs.',
    description:
      'Receives a `M_HidJoystick::EventButton_Args` struct atomically on `args` (fields `ButtonIndex`, `ButtonName`, `IsSelected`). For each configured output it checks whether the event matches by index or by name, then emits `IsSelected` on the corresponding Bool port. Receiving the whole event as one message avoids ordering races between index, name, and pressed state.',
    inputs: '`args` : M_HidJoystick::EventButton_Args (atomic struct input)',
    outputs: '— one Bool port per configured output; port name defaults to `button<index>` (byIndex) or the button name (byName)',
    config: '`outputs` — array of {`output`, `match` (byIndex/byName), `index`, `buttonName`}; `argsType` — struct type tag for `args` (default `M_HidJoystick::EventButton_Args`)',
    example: 'M_HidJoystick client EventButton.args → Input.ButtonHandler (outputs [{match byIndex, index 0}, {match byIndex, index 1}]) → two Bool outputs driving downstream logic.',
  },

  // ─────────────────────────────────────────── Flow & Structure ──────────────
  {
    type: 'Flow.StateMachine',
    category: 'Flow & Structure',
    title: 'State Machine',
    summary: 'Hierarchical state machine with edge-triggered transitions.',
    description:
      'A state machine is its own canvas: open it with the ⤢ button to author states and edges. Each transition fires on a boolean edge from an upstream port; entry/exit actions can write to outputs.',
    inputs: '— defined by the inner machine',
    outputs: '— defined by the inner machine',
    config: '`initial`, `states`, `transitions`',
    example: 'Use a state machine to model mode switches (Idle / Tracking / Lost).',
  },
  {
    type: 'Flow.Group',
    category: 'Flow & Structure',
    title: 'Group',
    summary: 'Wraps a sub-graph behind a reusable node with named ports.',
    description:
      'Open the group with ⤢ to author its internals. Expose any inner port using the ⇥/⇤ button on the inner node, or drop Group.Input/Group.Output terminals manually. Groups are flattened during Apply & Reload — they are an authoring-only convenience.',
    inputs: '— defined by inner Group.Input terminals',
    outputs: '— defined by inner Group.Output terminals',
    config: '`group` — the embedded sub-graph (nodes + edges)',
    example: 'Editor-only: select related nodes, right-click → Group to wrap them; the group appears as a single node in the parent canvas.',
  },
  {
    type: 'Note',
    category: 'Flow & Structure',
    title: 'Note',
    summary: 'A free-text annotation pinned to the canvas.',
    description:
      'Notes are never sent to the engine — they exist only for documentation in the editor. Edit the text in the Inspector; resize from the canvas.',
    example: 'Editor-only: drop a Note next to a complex pipeline to explain its purpose for future readers.',
  },
  {
    type: 'Group.Input',
    category: 'Flow & Structure',
    title: 'Group Input',
    summary: 'Boundary terminal that surfaces as an input port on the parent group.',
    description:
      'Only valid while editing a group canvas. The `portName` and `typeTag` set here define the name and type of the exposed input handle on the group node in the parent canvas.',
    outputs: '`out` : <typeTag> — value received from the parent graph',
    config: '`portName` — name of the exposed group input port; `typeTag` — data type',
    example: 'Editor-only: place inside a group canvas and connect its `out` to internal nodes to receive data from the parent graph.',
  },
  {
    type: 'Group.Output',
    category: 'Flow & Structure',
    title: 'Group Output',
    summary: 'Boundary terminal that surfaces as an output port on the parent group.',
    description:
      'Only valid while editing a group canvas. The `portName` and `typeTag` set here define the name and type of the exposed output handle on the group node in the parent canvas.',
    inputs: '`in` : <typeTag> — value to forward to the parent graph',
    config: '`portName` — name of the exposed group output port; `typeTag` — data type',
    example: 'Editor-only: place inside a group canvas and connect internal nodes to its `in` to expose data to the parent graph.',
  },

  // ─────────────────────────────────────────── OnboardAPI ────────────────────
  {
    type: 'OnboardApi.Discovery',
    category: 'OnboardAPI',
    title: 'Discovery',
    summary: 'Emits the list of OnboardAPI endpoints visible to this engine.',
    description:
      'When `trigger` receives true, queries the configured source and emits a `List` of endpoint objects on `out`. Each item carries `interfaceType`, `direction`, `serviceName`, and `domainId`; live items also include `hostName`, `procName`, `processId`, and `actorId`. Source `graph` lists nodes from the loaded workflow, `live` queries the DDS bus (real SDK only), and `both` returns their union de-duplicated.',
    inputs: '`trigger` : Bool — a true pulse starts the query',
    outputs: '`out` : List<OnboardApiEndpoint> — discovered endpoint records',
    config: '`source` — `graph`, `live`, or `both` (default `both`); `domainId` — DDS domain to query (default 1); `allDomains` — include graph endpoints from every domain (Bool)',
    example: 'Sources.Timer `tick` → OnboardApi.Discovery (source both) to re-query endpoints periodically; the list → Transform.List.Size → Sinks.Log.',
  },
  {
    type: 'OnboardApi.DeviceReport',
    category: 'OnboardAPI',
    title: 'Device Report',
    summary: 'Publishes this engine as a discoverable OnboardAPI Device service.',
    description:
      'A port-less node: simply placing it on the canvas is enough. While the graph runs it opens an `M_Device` Service on the configured DDS domain and publishes the engine version, git identity, build metadata, dependency list, active workflow details, and runtime status. It responds to `CmdIsDeviceAlive` and `CmdEnableDevice` commands and re-publishes at each heartbeat interval. Requires the real OnboardAPI SDK; under the stub build it logs the report instead.',
    inputs: '—',
    outputs: '—',
    config: '`domainId` — DDS domain for the Device service (default 1); `serviceName` — service registration name (default `Flowboard`); `deviceName` — reported device name; `description` — free-text description; `heartbeatSec` — re-publish interval in seconds, 0 to disable (default 5)',
    example: 'Drop OnboardApi.DeviceReport (serviceName "MyApp") onto the canvas; any OnboardAPI monitor on the same domain will discover and display the engine as a device.',
  },

  // ─────────────────────────────────────────── Other ─────────────────────────
  {
    type: 'Can.Bus',
    category: 'Other',
    title: 'CAN Bus',
    summary: 'Bridges the graph to a real or virtual CAN bus via a pluggable adapter.',
    description:
      'The gateway between your graph and an actual CAN bus. It transmits each `CanFrame` arriving on `tx` and emits every frame received from the bus on `rx`; `sent` reports per-frame transmit success (Bool) and `connected` tracks the open/closed state. The `adapter` spec is `<name>:<bus-id>` — e.g. `virtual:bus0` (in-process, needs no hardware; two Can.Bus nodes on the same virtual bus exchange frames), `socketcan:can0` (Linux), or a vendor plugin like `peak:0`. `listenOnly` makes it receive-only (drops `tx`, emits `sent`=false); with `autoReconnect` it reopens the bus every `reconnectIntervalMs` after a drop. It carries whole frames only — build them with Can.Encode and consume them with Can.Filter / Can.Decode.',
    inputs: '`tx` : CanFrame — frame to transmit',
    outputs: '`rx` : CanFrame — received frames; `sent` : Bool — per-tx result; `connected` : Bool — bus state',
    config:
      '`adapter` (`name:bus-id`), `bitrate`, `listenOnly`, `autoReconnect`, `reconnectIntervalMs`',
    example: 'Can.Encode → Can.Bus.tx; Can.Bus.rx → Can.Filter → Can.Decode.',
  },
  {
    type: 'Can.Filter',
    category: 'Other',
    title: 'CAN Filter',
    summary: 'Forwards only CAN frames whose arbitration ID passes the configured test.',
    description:
      'Passes a `CanFrame` from `in` to `out` only when its arbitration `id` matches; non-matching frames are dropped. `mode` chooses the test: `acceptAll` (pass everything), `acceptIds` / `rejectIds` (allow- or block-list the `ids` array), `idRange` (keep `idMin`…`idMax` inclusive), or `mask` (keep frames where `id & idMask === idMatch` — matches a group of IDs by bit pattern). Only the ID is examined; the payload and DLC pass through untouched. Typically placed right after `Can.Bus.rx` to drop traffic you do not care about.',
    inputs: '`in` : CanFrame',
    outputs: '`out` : CanFrame — only frames whose ID passes',
    config: '`mode` (acceptAll/acceptIds/rejectIds/idRange/mask), `ids`, `idMin`/`idMax`, `idMask`/`idMatch`',
    example: 'Can.Bus.rx → Can.Filter (mode acceptIds, ids [0x100]) → Can.Decode.',
  },
  {
    type: 'Can.Encode',
    category: 'Other',
    title: 'CAN Encode',
    summary: 'Assembles a CanFrame from an id, a List<UInt8> payload and the extended flag.',
    description:
      'Builds a `CanFrame` from its parts — the inverse of Can.Decode. It latches `id` (UInt32, the arbitration ID), `data` (List<UInt8> payload) and `isExtended` (Bool: 29-bit extended ID vs 11-bit standard), and emits a frame with `dlc` set to the payload length. Emits on a `trigger` pulse and, if "auto-emit on input" (`autoTriggerOnNewInput`) is set, on any input change. Unwired inputs use their inline default on the canvas node.',
    inputs: '`id` : UInt32; `data` : List<UInt8>; `isExtended` : Bool; `trigger` : Bool',
    outputs: '`out` : CanFrame',
    config: '`autoTriggerOnNewInput`',
    example: 'Bytes.Pack → data, ConstantSource(id) → Can.Encode → Can.Bus.tx.',
  },
  {
    type: 'Can.Decode',
    category: 'Other',
    title: 'CAN Decode',
    summary: 'Splits a CanFrame into its id, payload, extended flag and DLC.',
    description:
      'Breaks an incoming `CanFrame` into its fields — the inverse of Can.Encode. On each `frame` it emits `id` (UInt32), `data` (List<UInt8> payload), `isExtended` (Bool) and `dlc` (UInt8, the declared length). Feed `data` into Bytes.Unpack to pull typed signals out of the payload.',
    inputs: '`frame` : CanFrame',
    outputs: '`id` : UInt32; `data` : List<UInt8>; `isExtended` : Bool; `dlc` : UInt8',
    example: 'Can.Bus.rx → Can.Filter → Can.Decode → data → Bytes.Unpack.',
  },
  {
    type: 'Framer.Delimiter',
    category: 'Other',
    title: 'Framer · Delimiter',
    summary: 'Re-cuts a byte stream into frames split on a delimiter byte sequence.',
    description:
      'Buffers the incoming byte stream and ends a frame each time the `delimiter` byte sequence appears — boundaries come from a **separator in the data**, so frames can be any length. The delimiter is dropped from the output unless `includeDelimiter` is set; use `delimiterIsHex` to match a binary delimiter (e.g. `0d0a`). Bytes accumulated past `maxFrameSize` without a delimiter are discarded. Best for text/line protocols. Choose this when frames are separated by a marker; use `Framer.LengthPrefix` when each frame carries its own length, or `Framer.Fixed` when every frame is the same size.',
    inputs: '`in` : List<UInt8>',
    outputs: '`out` : List<UInt8> — one emit per delimited frame (delimiter stripped by default)',
    config: '`delimiter`, `delimiterIsHex`, `includeDelimiter`, `maxFrameSize`',
    example: 'Serial.Port.rx → Framer.Delimiter (delimiter "\\n") → Bytes.Unpack.',
  },
  {
    type: 'Framer.Fixed',
    category: 'Other',
    title: 'Framer · Fixed',
    summary: 'Re-cuts a byte stream into constant-size frames.',
    description:
      'Buffers the incoming byte stream and emits a frame every time `frameSize` bytes have accumulated. **Every frame is exactly the same length** — no length or delimiter is read from the data, so you must know the record size up front. Best when the device sends equal-size records (e.g. a fixed 8-byte sample every cycle). For variable-length messages use `Framer.LengthPrefix` (a length header in the stream) or `Framer.Delimiter` (a separator byte) instead.',
    inputs: '`in` : List<UInt8>',
    outputs: '`out` : List<UInt8> — one emit per `frameSize` bytes',
    config: '`frameSize`',
    example: 'Serial.Port.rx → Framer.Fixed (frameSize 8) → Bytes.Unpack.',
  },
  {
    type: 'Framer.LengthPrefix',
    category: 'Other',
    title: 'Framer · Length-Prefix',
    summary: 'Re-cuts a byte stream into variable-size frames sized by an in-stream length header.',
    description:
      'Buffers the incoming byte stream and frames variable-length messages that **carry their own size**: it reads a `prefixSize` (1, 2 or 4-byte, `littleEndian`) length header in front of each frame, waits until the whole body has arrived, then emits **just the body with the prefix stripped**. `lengthIncludesPrefix` selects whether that count covers the header bytes or only the payload. Frames larger than `maxFrameSize` (or with an invalid length) are dropped and the framer resyncs by one byte. Best for `[length][payload]` / TLV protocols. Use `Framer.Fixed` instead when every frame is the same size, or `Framer.Delimiter` when frames are separated by a marker byte.',
    inputs: '`in` : List<UInt8>',
    outputs: '`out` : List<UInt8> — one emit per frame (length prefix removed)',
    config: '`prefixSize` (1/2/4), `littleEndian`, `lengthIncludesPrefix`, `maxFrameSize`',
    example: 'Bytes `03 41 42 43` with prefixSize 1 → emits `41 42 43`.',
  },
  {
    type: 'Plugin.Scale',
    category: 'Other',
    title: 'Plugin · Scale (example)',
    summary: 'Example plugin: multiplies `in` by a configurable `factor`.',
    description:
      'Shipped as part of the hello_plugin example (see `examples/plugins/hello_plugin`). On every Double arriving on `in` the node multiplies it by `factor` and emits the result on `out`. Use it as a minimal reference when writing your own plugin node.',
    inputs: '`in` : Double',
    outputs: '`out` : Double',
    config: '`factor` : number (default 2.0)',
    example: 'ConstantSource (value 5.0) → Plugin.Scale (factor 3.0) → ValueDisplay → shows 15.',
  },
  {
    type: 'Plugin.Sum',
    category: 'Other',
    title: 'Plugin · Sum (example)',
    summary: 'Example plugin: emits the sum of two Double inputs.',
    description:
      'Shipped alongside Plugin.Scale in the hello_plugin example. Waits until both `a` and `b` have produced at least one value, then emits `a + b` on every subsequent change to either input. Use it as a reference for two-input plugin nodes.',
    inputs: '`a` : Double; `b` : Double',
    outputs: '`out` : Double',
    example: 'ConstantSource (3.0) → Plugin.Sum.a; ConstantSource (4.0) → Plugin.Sum.b → shows 7.',
  },
  {
    type: 'Python.Script',
    category: 'Other',
    title: 'Python Script',
    summary: 'Runs a Python script with declared input/output ports.',
    description:
      'Declare the node ports via `inputs` / `outputs` (name + type), then write a `def on_input(port, value)` handler that calls `emit(name, value)` to produce results. The script runs in a dedicated Python subprocess spawned at graph start; `pythonExe` overrides the interpreter. See `docs/PYTHON_NODE.md` for the full contract and type-tag list.',
    inputs: '— declared via `inputs`',
    outputs: '— declared via `outputs`',
    config: '`pythonExe`, `script`, `inputs`, `outputs`',
    example: 'Declare input x (Double), output y (Double); script: `emit("y", x * 2)` — ConstantSource (21) → Python.Script → ValueDisplay shows 42.',
  },
  {
    type: 'Serial.Port',
    category: 'Other',
    title: 'Serial Port',
    summary: 'Reads and writes a serial port; emits received bytes as List<UInt8> chunks.',
    description:
      'Opens the named port (`port`, e.g. COM3 or /dev/ttyUSB0) with the configured `baudRate`, `dataBits`, `parity`, `stopBits`, and `flowControl`. Incoming bytes are emitted on `rx` in chunks up to `readChunkSize`; byte arrays arriving on `tx` are written to the port. `connected` goes true when the port is open and false on error or close. With `autoReconnect` enabled the node reopens the port after `reconnectIntervalMs` ms on any failure.',
    inputs: '`tx` : List<UInt8>',
    outputs: '`rx` : List<UInt8>; `connected` : Bool',
    config:
      '`port`, `baudRate`, `dataBits`, `parity`, `stopBits`, `flowControl`, `readChunkSize`, `readTimeoutMs`, `autoReconnect`, `reconnectIntervalMs`',
    example: 'Serial.Port.rx → Framer.Delimiter → Bytes.Unpack (parse fields); Bytes.Pack → Serial.Port.tx (send command).',
  },

  // ─────────────────────────────────────────── Bytes ────────────────────────
  {
    type: 'Bytes.Pack',
    category: 'Bytes',
    title: 'Bytes Pack',
    summary: 'Places typed primitive inputs at byte offsets into a List<UInt8>.',
    description:
      'Each configured field becomes a typed input written at its byte offset (and optional bit offset 0–7 within that byte, LSB-first) using the node’s endianness; the output is a List<UInt8>. Emits on `trigger` and, if "auto-emit on input" is set, on any field change. Unwired fields use their inline default on the canvas node.',
    inputs: '`<field>` per configured field, plus `trigger` : Bool',
    outputs: '`out` : List<UInt8>',
    config: '`fields` — name/type/offset/bitOffset rows; `endian` — little/big (bit offsets are little/Intel only); `length` — output size (0 = auto); `autoTriggerOnNewInput`',
    example: 'Pack id (UInt8) + value (Int16) → List<UInt8> → Can.Encode.',
  },
  {
    type: 'Bytes.Unpack',
    category: 'Bytes',
    title: 'Bytes Unpack',
    summary: 'Decodes typed primitive fields from a List<UInt8> at byte offsets.',
    description:
      'On each List<UInt8> arriving on `in`, every configured field is read from its byte offset (and optional bit offset 0–7 within that byte, LSB-first) using the node’s endianness and emitted on its own typed output. Fields whose bytes are missing are skipped.',
    inputs: '`in` : List<UInt8>',
    outputs: '`<field>` per configured field',
    config: '`fields` — name/type/offset/bitOffset rows; `endian` — little/big (bit offsets are little/Intel only)',
    example: 'Can.Decode → data (List<UInt8>) → Bytes.Unpack → fields.',
  },

  // ─────────────────────────────────────────── OnboardAPI generated ──────────
  {
    type: '__OnboardAPI_Generated__',
    category: 'OnboardAPI · Generated',
    title: 'Generated OnboardAPI Nodes',
    summary:
      'M_<X>.Service / M_<X>.Client / Factory.M_<X>::<Struct> — produced by pipegen from the OnboardAPI data model.',
    description:
      'Each OnboardAPI interface (M_Alert, M_Mount, M_Camera, …) ships with a Service and a Client node whose ports mirror the interface\'s Report / Notify / Cmd / Config operations. Per operation, each parameter becomes a port named `Operation.Parameter`.\n\nFactory.M_<X>::<Struct> nodes assemble struct values from their scalar fields, so you can wire individual numbers/strings into a single struct port.\n\nFor full details on each interface, consult the Doxygen API reference or query a running engine at `GET /api/types`.',
  },
];

const BY_TYPE = new Map<string, NodeHelp>(NODE_HELP.map(h => [h.type, h]));

// Lookup with fallback for OnboardAPI generated types: returns the umbrella
// entry so the help icon on any M_<X>.* / Factory.* node opens to a useful
// description. Returns `undefined` only for truly unknown types.
export function helpForType(typeName: string): NodeHelp | undefined {
  const direct = BY_TYPE.get(typeName);
  if (direct) return direct;
  if (typeName.startsWith('M_') || typeName.startsWith('Factory.')) {
    return BY_TYPE.get('__OnboardAPI_Generated__');
  }
  return undefined;
}

// Anchor used by HelpPanel for in-document scrolling. The encoding survives
// dots and double-colons (CSS selectors don't like raw "::"); the panel just
// uses it as a plain string key.
export function helpAnchorId(typeName: string): string {
  return 'help-' + typeName.replace(/[^A-Za-z0-9_.-]/g, '_');
}
