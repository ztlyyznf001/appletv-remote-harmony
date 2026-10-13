import { UIAbility } from '@kit.AbilityKit';
import { window } from '@kit.ArkUI';
import { remoteController } from '../model/RemoteController';

export default class EntryAbility extends UIAbility {
  onCreate(): void { remoteController.initialize(this.context); }
  onWindowStageCreate(stage: window.WindowStage): void {
    stage.loadContent('pages/Index', (error) => {
      if (error.code) {
        console.error('Unable to load remote page');
        return;
      }
      // Window background requires initialized UIContent, available only after loadContent.
      try {
        const mainWindow: window.Window = stage.getMainWindowSync();
        mainWindow.setWindowBackgroundColor('#101719');
        mainWindow.setWindowSystemBarProperties({
          statusBarColor: '#101719',
          navigationBarColor: '#101719',
          statusBarContentColor: '#E6EEEA',
          navigationBarContentColor: '#E6EEEA'
        }).catch(() => { console.warn('Unable to apply remote system bar appearance'); });
      } catch (_) {
        // Appearance is optional: a window transition must never abort application startup.
        console.warn('Unable to apply remote window appearance');
      }
    });
  }
  onForeground(): void { remoteController.enterForeground(); }
  onBackground(): void { remoteController.leaveForeground(); }
  onDestroy(): void { remoteController.leaveForeground(); }
}
