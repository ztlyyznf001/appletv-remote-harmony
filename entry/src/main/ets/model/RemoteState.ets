import { Device } from './Device';

@Observed
export class RemoteState {
  ready: boolean = false;
  foreground: boolean = false;
  devices: Device[] = [];
  selected: Device | undefined = undefined;
  discoveryText: string = '正在初始化…';
  status: string = '选择 Apple TV，开始配对';
  busy: boolean = false;
  buttonInFlight: boolean = false;
  connected: boolean = false;
  needsPin: boolean = false;
  pin: string = '';
  canForget: boolean = false;
  generation: number = 0;
}
