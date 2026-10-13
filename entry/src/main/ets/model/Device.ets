export class Device {
  id: string;
  name: string;
  host: string;
  port: number;
  constructor(id: string, name: string, host: string, port: number) {
    this.id = id; this.name = name; this.host = host; this.port = port;
  }
}

/** Numeric addresses only. Parsing never invokes DNS or guesses a Companion port.
 * IPv6 permits hexadecimal groups, one :: compression, and an optional numeric scope.
 * Embedded dotted IPv4 forms are intentionally not accepted by this entry form.
 * Native inet_pton provides the final address validation before opening a socket.
 */
export function manualEndpoint(address: string, portText: string): Device | undefined {
  if (!/^[0-9]{1,5}$/.test(portText.trim())) return undefined;
  const port: number = Number(portText.trim());
  if (port < 1 || port > 65535) return undefined;
  let host: string = address.trim();
  if (host.startsWith('[') && host.endsWith(']')) host = host.slice(1, -1);
  if (host.includes(':')) {
    const ipv6: string | undefined = numericIpv6(host);
    if (ipv6 === undefined) return undefined;
    host = ipv6;
  } else {
    const pieces: string[] = host.split('.');
    if (pieces.length !== 4) return undefined;
    const octets: number[] = [];
    for (const piece of pieces) {
      if (!/^[0-9]{1,3}$/.test(piece)) return undefined;
      const value: number = Number(piece);
      if (value > 255) return undefined;
      octets.push(value);
    }
    if (octets[0] === 0 || octets[0] === 127 || octets[0] >= 224) return undefined;
    host = octets.join('.');
  }
  return new Device(`manual:${host}:${port}`, 'Apple TV（手动）', host, port);
}

function numericIpv6(address: string): string | undefined {
  const scoped: string[] = address.split('%');
  if (scoped.length > 2) return undefined;
  let scope: string = '';
  if (scoped.length === 2) {
    if (!/^[0-9]{1,10}$/.test(scoped[1])) return undefined;
    const value: number = Number(scoped[1]);
    if (value < 1 || value > 4294967295) return undefined;
    scope = '%' + value.toString();
  }
  const halves: string[] = scoped[0].split('::');
  if (halves.length > 2) return undefined;
  const left: string[] = halves[0].length === 0 ? [] : halves[0].split(':');
  const right: string[] = halves.length === 2 && halves[1].length > 0 ? halves[1].split(':') : [];
  if (halves.length === 1 && left.length !== 8) return undefined;
  if (halves.length === 2 && left.length + right.length >= 8) return undefined;
  const groups: number[] = [];
  for (const part of left) {
    if (!/^[0-9a-fA-F]{1,4}$/.test(part)) return undefined;
    groups.push(Number.parseInt(part, 16));
  }
  if (halves.length === 2) {
    const zeros: number = 8 - left.length - right.length;
    for (let index: number = 0; index < zeros; ++index) groups.push(0);
  }
  for (const part of right) {
    if (!/^[0-9a-fA-F]{1,4}$/.test(part)) return undefined;
    groups.push(Number.parseInt(part, 16));
  }
  if (groups.length !== 8 || (groups[0] & 0xff00) === 0xff00) return undefined;
  const leadingZero: boolean = groups.slice(0, 7).every((value: number) => value === 0);
  if (leadingZero && groups[7] <= 1) return undefined;
  return groups.map((value: number) => value.toString(16)).join(':') + scope;
}
