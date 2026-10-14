/** Wire phases match Companion/Android; HOLD is an internal SELECT-down marker. */
export class TouchSample {
  static readonly BEGIN: number = 1;
  static readonly HOLD: number = 2;
  static readonly MOVE: number = 3;
  static readonly END: number = 4;
  x: number;
  y: number;
  phase: number;
  tap: boolean;
  cancelled: boolean;

  constructor(x: number, y: number, phase: number, tap: boolean = false, cancelled: boolean = false) {
    this.x = Number.isFinite(x) ? Math.max(0, Math.min(1000, Math.round(x))) : x;
    this.y = Number.isFinite(y) ? Math.max(0, Math.min(1000, Math.round(y))) : y;
    this.phase = phase;
    this.tap = tap;
    this.cancelled = cancelled;
  }
}

/** One Promise in flight. Only the latest unsent MOVE is retained on either side
 * of the single HOLD; BEGIN and END cannot be coalesced away. A gesture therefore
 * retains at most five samples, regardless of pointer frequency/network latency.
 */
export class TouchGesturePump {
  private queue: TouchSample[] = [];
  private running: boolean = false;
  private started: boolean = false;
  private finished: boolean = false;
  private aborted: boolean = false;
  private held: boolean = false;
  private lastX: number = 0;
  private lastY: number = 0;
  private terminal: TouchSample | undefined = undefined;
  private send: (sample: TouchSample) => Promise<void>;
  private failed: (error: Error) => void;

  constructor(send: (sample: TouchSample) => Promise<void>, failed: (error: Error) => void) {
    this.send = send;
    this.failed = failed;
  }

  offer(sample: TouchSample): boolean {
    if (this.finished || this.aborted || !Number.isFinite(sample.x) || !Number.isFinite(sample.y)) return false;
    if (sample.phase !== TouchSample.BEGIN && sample.phase !== TouchSample.MOVE &&
      sample.phase !== TouchSample.HOLD && sample.phase !== TouchSample.END) return false;
    if (!this.started) {
      if (sample.phase !== TouchSample.BEGIN) return false;
      this.started = true;
    } else if (sample.phase === TouchSample.BEGIN) return false;
    if (sample.phase === TouchSample.HOLD) {
      if (this.held) return false;
      this.held = true;
    } else {
      this.lastX = sample.x;
      this.lastY = sample.y;
    }
    if (sample.phase === TouchSample.MOVE && this.queue.length > 0 &&
      this.queue[this.queue.length - 1].phase === TouchSample.MOVE) this.queue.pop();
    if (sample.phase === TouchSample.END) {
      sample.tap = sample.tap && !this.held && !sample.cancelled;
      this.terminal = sample;
      this.finished = true;
    }
    this.queue.push(sample);
    this.startDrain();
    return true;
  }

  /** Cancel may revoke a tap even after END was submitted and is awaiting I/O.
   * Do not send a queued HOLD for a finger that has already been cancelled.
   */
  cancel(): void {
    if (!this.started || this.aborted) return;
    if (this.terminal !== undefined) {
      this.terminal.tap = false;
      this.terminal.cancelled = true;
      this.queue = this.queue.filter((sample: TouchSample) =>
        sample.phase === TouchSample.BEGIN || sample.phase === TouchSample.END);
    } else {
      this.queue = this.queue.filter((sample: TouchSample) => sample.phase === TouchSample.BEGIN);
      this.offer(new TouchSample(this.lastX, this.lastY, TouchSample.END, false, true));
    }
  }

  /** Used only when the owning session is invalidated and its transport closed. */
  abort(): void {
    this.aborted = true;
    this.finished = true;
    this.queue = [];
    if (this.terminal !== undefined) { this.terminal.tap = false; this.terminal.cancelled = true; }
  }

  private startDrain(): void {
    if (this.running || this.aborted) return;
    this.running = true;
    this.drain();
  }

  private async drain(): Promise<void> {
    try {
      while (!this.aborted && this.queue.length > 0) {
        const sample: TouchSample | undefined = this.queue.shift();
        if (sample !== undefined) await this.send(sample);
      }
    } catch (error) {
      if (!this.aborted) { this.abort(); this.failed(error as Error); }
    } finally {
      this.running = false;
    }
  }
}
