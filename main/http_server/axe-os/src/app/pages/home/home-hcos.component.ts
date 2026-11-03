import { ChangeDetectionStrategy, Component, inject, NgZone, OnDestroy } from '@angular/core';
import { HttpClient } from '@angular/common/http';

import { HomeComponent } from './home.component';

/** One share from the Hash Commander OS share log. st: 0 waiting, 1 accepted, 2 rejected, 3 no answer */
interface HcShare { seq: number; ts: number; diff: number; st: number; isNew?: boolean; }

/**
 * Dashboard for the "HC OS" theme (Hash Commander OS).
 *
 * Inherits all data handling (polling, chart, history, lifecycle) from HomeComponent, like the Gaia
 * dashboard, and adds the Hash Commander OS parts: the live share feed (WebSocket /api/hc/live, the
 * log itself from /api/hc/shares), the boot guard status and the way back to the official firmware.
 */
@Component({
  selector: 'app-home-hcos',
  templateUrl: './home-hcos.component.html',
  styleUrls: ['./home.component.scss', './home-hcos.component.scss'],
  changeDetection: ChangeDetectionStrategy.Default,
})
export class HomeHcosComponent extends HomeComponent implements OnDestroy {
  protected override get hashrateColor(): string {
    return '#ff4fd8';
  }

  private readonly http = inject(HttpClient);
  private readonly zone = inject(NgZone);

  public shares: HcShare[] = [];
  public hcInfo: any = null;
  public live = false;
  public bestSeen = 0;
  private ws?: WebSocket;
  private wsRetry?: any;
  private infoTimer?: any;
  private closed = false;

  override ngOnInit(): void {
    super.ngOnInit();
    this.loadHcInfo();
    this.http.get<any>('/api/hc/shares?limit=14').subscribe({
      next: (r) => {
        this.shares = (r?.items || []).reverse().map((x: number[]) => ({ seq: x[0], ts: x[1], diff: x[3], st: x[7] }));
        this.bestSeen = Math.max(0, ...this.shares.map((s) => s.diff));
      },
      error: () => {},
    });
    this.infoTimer = setInterval(() => this.loadHcInfo(), 15000);
    this.connect();
  }

  override ngOnDestroy(): void {
    this.closed = true;
    clearInterval(this.infoTimer);
    clearTimeout(this.wsRetry);
    try { this.ws?.close(); } catch { /* ignore */ }
    super.ngOnDestroy();
  }

  private loadHcInfo(): void {
    this.http.get<any>('/api/hc/info').subscribe({ next: (r) => (this.hcInfo = r), error: () => {} });
  }

  /** Live shares pushed by the miner; reconnects by itself (the miner accepts 3 live clients). */
  private connect(): void {
    if (this.closed) return;
    const proto = location.protocol === 'https:' ? 'wss' : 'ws';
    this.zone.runOutsideAngular(() => {
      const ws = new WebSocket(`${proto}://${location.host}/api/hc/live`);
      this.ws = ws;
      ws.onopen = () => this.zone.run(() => (this.live = true));
      ws.onclose = () => this.zone.run(() => {
        this.live = false;
        if (!this.closed) this.wsRetry = setTimeout(() => this.connect(), 5000);
      });
      ws.onmessage = (ev) => {
        let m: any;
        try { m = JSON.parse(ev.data); } catch { return; }
        if (m.t !== 'share') return;
        this.zone.run(() => {
          const i = this.shares.findIndex((s) => s.seq === m.seq);
          const s: HcShare = { seq: m.seq, ts: m.ts, diff: m.diff, st: m.st, isNew: true };
          if (i >= 0) { this.shares[i] = { ...s, isNew: this.shares[i].isNew }; return; }   // result of a share already shown
          this.shares.forEach((x) => (x.isNew = false));
          this.shares.unshift(s);
          this.shares = this.shares.slice(0, 14);
          if (m.diff > this.bestSeen) this.bestSeen = m.diff;
        });
      };
    });
  }

  // ── helpers for the template ────────────────────────────────────────────────
  public clamp100(n: number): number { return Math.max(0, Math.min(100, Number(n) || 0)); }

  /** stroke-dasharray for the hashrate ring (r = 70) */
  public ringDash(pct: number): string {
    const C = 2 * Math.PI * 70;
    return `${((this.clamp100(pct) / 100) * C).toFixed(1)} ${C.toFixed(1)}`;
  }

  public diff(d: number): string {
    const u = ['', 'k', 'M', 'G', 'T', 'P'];
    let v = Number(d) || 0, i = 0;
    while (v >= 1000 && i < u.length - 1) { v /= 1000; i++; }
    return (i ? v.toFixed(1) : v.toFixed(0)) + (u[i] ? ' ' + u[i] : '');
  }

  public time(ts: number): string {
    return ts ? new Date(ts * 1000).toLocaleTimeString() : '—';
  }

  public stLabel(st: number): string {
    return ['a enviar', 'aceite', 'rejeitada', 'sem resposta'][st] || '';
  }

  public chips(info: any): number[] {
    const c = info?.performance?.chips;
    return Array.isArray(c) ? c.map((x: any) => Number(x?.hashRate) || 0) : [];
  }

  public chipPct(v: number, all: number[]): number {
    const max = Math.max(...all, 1);
    return this.clamp100((v / max) * 92);
  }

  public chipLow(v: number, all: number[]): boolean {
    const avg = all.reduce((a, b) => a + b, 0) / Math.max(all.length, 1);
    return avg > 0 && v < avg * 0.9;
  }

  /** seconds per turn for the fan icon, slowed down 8x so it still reads as a fan */
  public fanSpin(rpm: number): string {
    const r = Number(rpm) || 0;
    return r > 0 ? `${Math.min(6, (60 / r) * 8).toFixed(2)}s` : '0s';
  }

  public tempPct(t: number): number { return this.clamp100(((Number(t) || 0) - 25) / 50 * 100); }

  public tempHot(t: number): boolean { return (Number(t) || 0) >= 65; }

  public exportShares(): void {
    this.http.get<any>('/api/hc/shares?limit=500').subscribe((r) => {
      const st = ['pending', 'accepted', 'rejected', 'no answer'];
      const rows = (r?.items || []).map((x: number[]) =>
        [x[0], x[1] ? new Date(x[1] * 1000).toISOString() : '', x[2], x[3], x[4], x[5] ? 'fallback' : 'primary', x[6], st[x[7]] || x[7]].join(','));
      const csv = 'seq,time,uptime_s,difficulty,pool_difficulty,pool,asic,result\n' + rows.join('\n');
      const a = document.createElement('a');
      a.href = URL.createObjectURL(new Blob([csv], { type: 'text/csv' }));
      a.download = 'hcos-shares.csv';
      a.click();
      setTimeout(() => URL.revokeObjectURL(a.href), 2000);
    });
  }

  public backToOfficial(): void {
    const prev = this.hcInfo?.bootGuard?.fallback || '?';
    if (!confirm(`Voltar ao firmware que estava instalado antes do Hash Commander OS (${prev})? A máquina reinicia.`)) return;
    this.http.post('/api/hc/rollback', '', { responseType: 'text' }).subscribe({
      next: () => alert('A máquina está a voltar ao firmware anterior e vai reiniciar.'),
      error: (e) => alert(e?.status === 401 ? 'A máquina pediu o código OTP (ativa a sessão OTP em Security).' : 'Não foi possível voltar atrás: ' + (e?.error || e?.message || '')),
    });
  }
}
