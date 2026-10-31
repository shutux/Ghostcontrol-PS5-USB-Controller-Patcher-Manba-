# Patch USB Ghost-Control Manba V2 pour Ghost control / StonedModder



La base reste celle de Ghost-Control de StonedModder : lire une manette USB supportee, traduire
ses rapports d'input, creer une manette virtuelle PS5, puis injecter des
`ScePadData`.

Le travail ajoute ici concerne surtout :

- le support Manba V2 en USB ;
- le mode Manba PC/XInput ;
- le mode Manba Switch USB/dongle ;
- la correction de l'axe Y en mode Switch Manba ;
- le comportement propre entre la Manba et la manette officielle quand elles
  essayent de prendre le meme user ;
- Un peu de notes de recherche Bluetooth separees, car le Bluetooth n'est pas resolu
  dans cette ELF, encore en recherche...

## Credits

- Projet/tool original : StonedModder,
  `Ghostcontrol-PS5-USB-Controller-Patcher`
  - https://github.com/StonedModder/Ghostcontrol-PS5-USB-Controller-Patcher
- Tests Manba V2 NBJr USB, validation PS5, verification des axes, tests
  changement user et tests Manba/officielle .
- PS5 SDK John tromblom 

## Contenu Du Dossier

```text
ELF/
  GhostControl-Cleanup.elf
  GhostControl-ManbaV2-NBJr-USB-Patch.elf

SOURCE MODIFIEE PAYLOAD/
  Sources modifiees utilisees pour compiler la payload finale testee OK

Launch-GhostControl-ManbaV2-NBJr.bat
Launch-GhostControl-ManbaV2-NBJr.ps1

```

## DS5Dongle（Pico）接到这台 PS5

PS5 认物理 DualSense 时会做加密认证。普通 DS5Dongle 的 Sony VID/PID
会先被系统原生手柄驱动抢走，然后因为认证失败而消失。配套固件默认开启
`ps5_ghost_mode=1`，改用中性 USB 身份 `CAFE:0D55`，让原生驱动不再抢设备。

这份 payload 不冒充 USB 手柄。它从 `/dev/ugen` 读 DS5Dongle 的报告（report `0x01`），按 DualSense 的字节布局翻译，再用 `scePadVirtualDeviceInsertData` 注入一个虚拟 DualSense。认证被绕开。

用法：

1. 手柄先和 Pico 配对。dongle 在蓝牙连上之前会把 USB 藏起来，PS5 这时看不到设备。
2. 确认 `ps5_ghost_mode=1`（配套固件默认开启）。本地工具可用
   `python tools/config_tool.py set ps5_ghost_mode=1` 设置；切换后重新插拔 Pico。
3. Pico 插到 PS5，按一下 PS 键让手柄连上 dongle。等通知里出现 `DS5Dongle`，再按任意键分配用户。
4. 用原来的启动脚本注入 `ELF/GhostControl-ManbaV2-NBJr-USB-Patch.elf`；
   该 ELF 已包含 DS5Dongle 映射。

已经映射：摇杆、十字键、面部键、肩键、L2/R2 模拟、Create、Options、PS、触摸板按下、触摸坐标。

普通震动和自适应扳机也会回传：payload 在游戏进程中 hook
`scePadSetVibration` 和 `scePadSetTriggerEffect`，把输出转换为 DualSense USB
report `0x02`，再由 DS5Dongle 转成蓝牙 report `0x31` 发给真实手柄。
游戏 hook 来自 GPL-3.0 的
[PuckbridgePS5](https://github.com/ThisIsAkill/PuckbridgePS5) /
[PoorDS4](https://github.com/ItsBlurf/PoorDS4)，许可与版本说明在
`SOURCE MODIFIEE PAYLOAD/bridge/`。

目前不转发高清音频触觉、灯条和耳机音频。IMU 原始数据也没有融合成姿态，
`quat` 保持单位四元数。游戏必须实际调用上述两个 `scePad` API；只输出高清
触觉音频而不调用普通震动 API 的游戏不会产生普通震动。

## Ce Qui Fonctionne En USB

- Manba V2 en mode PC/XInput USB.
- Manba V2 en mode Switch USB/dongle.
- DS5Dongle Ghost mode (`CAFE:0D55`; legacy `054C:0CE6/0DF2` sans numero de serie) : rapport `0x01` mappe en `ScePadData`.
- Correction de l'axe Y en mode Switch Manba dans `payload/gc_main.c`.
- Quand la Manba devient active, la manette officielle se deconnectee proprement.
- Si la manette officielle est rallumee sur un autre user, les deux manettes
  peuvent rester actives pour jouer a deux.
- Si la manette officielle est rallumee sur le meme user que la Manba, la VDA
  Manba est liberee et l'officielle reprend la main.

## Recherche Bluetooth

Pendant des tests, la PS5 voyait bien la partie Bluetooth de la Manba, mais elle
se comportait comme un accessoire, pas comme une vraie manette `scePad`
utilisable. La popup profil/user pouvait parfois s'ouvrir, et la console pouvait
afficher deux manettes/accessoires, mais le chemin Bluetooth Manba ne donnait
pas un flux d'input stable

Points vus pendant les tests :

- Le transport Bluetooth etait visible comme device MediaTek :

```text
/dev/ugen0.2
VID:PID 0x0e8d:0x3603
manufacturer="MediaTek Inc."
product="Wireless_Device"
class=0xe0 sub=0x01 proto=0x01
```

- Ce device MediaTek indique le transport/adaptateur Bluetooth, pas les boutons
  de la manette.
- Le mode receiver/update de la Manba a aussi ete vu en `1a34:f517`.
- D'autres devices comme Realtek `0x0bda:0x9210` sont du bruit USB/adaptateur et
  ne doivent pas etre pris pour une manette.
- En USB, la payload a un vrai device `/dev/ugen*` et de vrais reports input sur
  l'endpoint `0x81`.
- En Bluetooth, on n'a pas obtenu le meme chemin de reports input lisibles.
- La manette officielle devient une vraie manette avec handles `scePad` et user.
- La Manba Bluetooth est restee sur un chemin accessoire, pas un pad `scePad`
  fiable.

Endroits recherches  :

- scan `/dev/ugen*` ;
- descriptors USB ;
- endpoints USB ;
- lignes klog `Open Pad` ;
- lignes klog `DEVICE_ADDED` ;
- ids MBus physiques finissant par `0x0300` ;
- ids virtuels crees par la payload ;
- `scePadInit` ;
- `scePadGetHandle` ;
- `scePadVirtualDeviceAddDevice` ;
- `scePadVirtualDeviceInsertData` ;
- `scePadVirtualDeviceDeleteDevice` ;
- `scePadSetProcessPrivilege` ;
- `SceShellUI` ;
- `SceShellCore` ;
- `libScePad` ;
- `libSceMbus` ;
- `sceMbusDisconnectDevice` ;
- `sceMbusBindDeviceWithUserId`.

Ce qui a servi dans le patch USB final :

- deconnexion/liberation de la manette officielle physique ;
- detection quand l'officielle reprend le meme user ;
- liberation de la VDA Manba quand l'officielle reprend ce user.

Ce qui n'est pas resolu :

- binder la Manba Bluetooth comme vraie manette ;
- lire de vrais reports input Bluetooth Manba ;
- assigner la Manba Bluetooth a un user comme une manette `scePad` normale.

 La Manba BT est vue par la PS5, mais elle reste bloquee avant le
chemin input jeu.

Points trouves :

- Adresse BT Manba confirmee :
  - normale : `98:b6:ea:bd:cd:58`
  - reverse en memoire : `58 cd bd ea b6 98`
- Dans `SceSysCore` / `SceMbusKmodEventPolling`, un event Manba a ete retrouve :
  - hit Manba reverse autour de `+0x1ff30`
  - debut event estime autour de `+0x1ff10`
  - signature event : `0x08 / 0x04 / 0x03`
  - `manba_hits_total=1`
  - `manba_hits_heap=0`
- Dans `SceMbusHeap`, la DualSense officielle apparait avec son BT et son
  VID/PID, mais la Manba n'a pas d'entree pad equivalente.
- La comparaison event officielle vs event Manba montre que l'event officiel
  porte BT + VID/PID ensemble, alors que l'event Manba porte la MAC mais pas de
  VID/PID Manba proche. Cela pointe vers un probleme de classification/promotion
  accessoire vers pad, pas vers un simple patch d'adresse.
- Les essais , et les tests
  `KMOD_TO_HEAP … ont confirme que ces pistes ne suffisent pas :
  - patch VID/PID ShellUI seul ;
  - patch SIG8 ShellUI ;
  - bind direct `0x190300` ;
  - bind direct `0x30300` ;
  - force `0x2030e` comme pad ;
  - fallback `0x2030e -> 0x190300` ;
  - activation table ShellUI `active20/user24/type28` avec ou sans user ;
  - scans simples `/dev/hid` et `/dev/bluetooth_hid`.
- Les tests  jeu montrent que `eboot.bin` a sa propre table `libScePad` :
  l'officielle y est active, mais la Manba BT reste cote Shell/Cdlg/accessoire.
- Le jeu appelle `scePadRead`.
  ...

Conclusion actuelle : le Bluetooth demande une recherche separee autour de la
pile Bluetooth PS5, HCI/L2CAP/HIDP, la logique accessoire vers pad, ou les
modules/PRX utilises par la DualSense officielle.


## Lancer La Payload Finale

1. Demarrer l'ecoute payload sur la PS5, port `9021`.
2. Lancer :

```powershell
.\Launch-GhostControl-ManbaV2-NBJr.ps1
```

ou double-cliquer :

```text
Launch-GhostControl-ManbaV2-NBJr.bat
```

3. Entrer l'IP de la PS5.
4. Lancer 1/2/3 
   1 : Kill ancienne payload et renjecte payload propre 
   2:  Elf ghost  ( active auto )
   3:  Kill seul

## Ou Regarder Pour Les Patchs

```
Les zones importantes :

- `payload/controller_ds5.h` / `controller_ds5.c`
  - DS5Dongle Ghost `CAFE:0D55`, plus legacy `054C:0CE6/0DF2` sans numero de serie.
  - Rapport `0x01` : sticks, gachettes, boutons, touchpad.
- `payload/controller_mamba.h`
  - VID/PID Manba.
  - mode PC/XInput.
  - mode Switch USB.
- `payload/controller_mamba.c`
  - mapping Manba PC/XInput.
  - axes XInput.
  - boutons XInput.
- `payload/gc_main.c`
  - detection Manba.
  - routage USB.
  - correction axe Y Switch Manba apres `nintendo_handle_packet()`.
  - logique de liberation/recreation VDA.
  - detection manette officielle qui reprend le meme user.
- `payload/shellui_pad.c`
  - fonctions ShellUI/MBus pour couper une manette physique.
  - verification handle/user.
- `payload/shellui_pad.h`
  - declarations des nouvelles fonctions.
- `payload/Makefile`
  - ajout de `controller_mamba.o`.

## Correction Switch Importante

La correction Switch n'est pas dans `controller_nintendo.c`.

Elle est dans :

```text
payload/gc_main.c
```

dans `usb_hid_thread`, apres :

```c
injected = nintendo_handle_packet(...);
```

avec :

```c
if (injected > 0 && is_mamba_switch)
    pad.leftStick.y = (uint8_t)(255u - pad.leftStick.y);
```

Pourquoi ici ?

Parce que si on inverse directement dans `controller_nintendo.c`, on risque de
casser les vraies manettes Switch Pro ou les autres pads compatibles Nintendo ( je n'ai pas de manettes pour test )
La correction doit rester limitee a la Manba reconnue comme `057e:2009`.

## Build

La compilation utilise l'environnement PS5 payload SDK…

Script utilise pendant les tests :

```text
payload/build_correction.sh
```

ELF finale testee OK :

```text
ELF/GhostControl-ManbaV2-NBJr-USB-Patch.elf
```

## Avertissement

Recherche uniquement a titre educatif . A utiliser a vos risques et perils . Tester uniquement avec mon environnement PS5 6.02 et Manba.
Je ne dispose pas d'outils , script ou payload magique universel malheuresement pour analyse plus approfondi pour support d autre manettes ou adapation BT,  je procede par etapes par etapes, cela demande du temps ...

