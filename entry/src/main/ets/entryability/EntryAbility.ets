import { UIAbility } from '@kit.AbilityKit';
import { window } from '@kit.ArkUI';
import { remoteController } from '../model/RemoteController';

export default class EntryAbility extends UIAbility {
  onCreate(): void { remoteController.initialize(this.context); }
  onWindowStageCreate(stage: window.WindowStage): void { stage.loadContent('pages/Index'); }
  onForeground(): void { remoteController.enterForeground(); }
  onBackground(): void { remoteController.leaveForeground(); }
  onDestroy(): void { remoteController.leaveForeground(); }
}
