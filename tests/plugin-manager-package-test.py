#!/usr/bin/env python3
"""Exercise OBS 33 install rules using real CMake staging and migration helpers."""
import json,os,pathlib,re,shutil,subprocess,tempfile,unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
CMAKE=os.environ.get('BH_TEST_CMAKE','cmake')
def run(*args,**kwargs):return subprocess.run([str(a) for a in args],check=True,capture_output=True,text=True,**kwargs)
class PackagingTests(unittest.TestCase):
 def test_cmake_stages_manager_layouts(self):
  cases=[('windows','BUNDLE','bacons-helper/bacons-helper.dll','bacons-helper/data'),('windows','LEGACY','obs-plugins/64bit/bacons-helper.dll','data/obs-plugins/bacons-helper'),('linux','BUNDLE','bacons-helper/bacons-helper.so','bacons-helper/data'),('linux','SYSTEM','lib/obs-modules/plugins/bacons-helper.so','share/obs/obs-modules/plugins/bacons-helper'),('linux','FLATPAK','lib/obs-modules/plugins/bacons-helper.so','share/obs/obs-modules/plugins/bacons-helper')]
  version=json.loads((ROOT/'buildspec.json').read_text())['version']
  self.assertRegex((ROOT/'CMakeLists.txt').read_text(),rf'project\(bacons-helper-obs VERSION {re.escape(version)} ')
  for platform,layout,binary,data in cases:
   with self.subTest(platform=platform,layout=layout),tempfile.TemporaryDirectory(prefix='bh-layout-') as tmp:
    d=pathlib.Path(tmp);src=d/'source';src.mkdir();(src/'data/locale').mkdir(parents=True);(src/'data/locale/en-US.ini').write_text('title=Bacons Helper\n');(src/'data/language-guesser').mkdir();(src/'data/language-guesser/model.json').write_text('{}')
    (src/'module.c').write_text('int bh_layout_test(void) { return 1; }\n')
    (src/'CMakeLists.txt').write_text(f'''cmake_minimum_required(VERSION 3.28)
project(bh-layout-test VERSION {version} LANGUAGES C)
set(WIN32 {"TRUE" if platform=="windows" else "FALSE"})
set(APPLE FALSE)
include("{(ROOT/'cmake/PluginInstall.cmake').as_posix()}")
add_library(bacons-helper MODULE module.c)
set_target_properties(bacons-helper PROPERTIES PREFIX "" SUFFIX ".{'dll' if platform=='windows' else 'so'}")
bh_install_plugin(bacons-helper "${{CMAKE_CURRENT_SOURCE_DIR}}/data")
''')
    run(CMAKE,'-S',src,'-B',d/'build',f'-DBH_OBS_INSTALL_LAYOUT={layout}','-DCMAKE_INSTALL_LIBDIR=lib','-DCMAKE_INSTALL_DATADIR=share');run(CMAKE,'--build',d/'build');run(CMAKE,'--install',d/'build','--prefix',d/'stage')
    self.assertTrue((d/'stage'/binary).is_file());self.assertTrue((d/'stage'/data/'locale/en-US.ini').is_file());self.assertTrue((d/'stage'/data/'language-guesser/model.json').is_file())
    metadata=json.loads((d/'stage'/data/'manifest.json').read_text());self.assertEqual(metadata['id'],'bacons-helper');self.assertEqual(metadata['version'],version);self.assertEqual(metadata['display_name'],'Bacons Helper OBS Companion');self.assertTrue(metadata['description']);self.assertTrue(metadata['os_arch'].startswith(platform));self.assertFalse(metadata['has_icon']);self.assertFalse(metadata['has_banner']);self.assertTrue(all(url.startswith('https://') for url in metadata['urls'].values()))
 def test_linux_installer_migrates_only_plugin_and_preserves_config(self):
  with tempfile.TemporaryDirectory(prefix='bh-install-') as tmp:
   d=pathlib.Path(tmp);package=d/'package';source=package/'bacons-helper';(source/'data/locale').mkdir(parents=True);(source/'data/language-guesser').mkdir();(source/'bacons-helper.so').write_text('new-module');(source/'data/manifest.json').write_text('{"id":"bacons-helper"}');(source/'data/locale/en-US.ini').write_text('locale');(source/'data/language-guesser/model.json').write_text('{}')
   home=d/'home';home.mkdir();data=home/'data';config=home/'config';legacy=config/'obs-studio/plugins/bacons-helper';legacy.mkdir(parents=True);(legacy/'old-module').write_text('old');settings=config/'obs-studio/plugin_config/bacons-helper';settings.mkdir(parents=True);(settings/'companion.ini').write_text('keep-credentials');plugins=data/'obs-studio/plugins';plugins.mkdir(parents=True);other=plugins/'another-plugin';other.mkdir();(other/'keep').write_text('safe')
   env={**os.environ,'HOME':str(home),'XDG_DATA_HOME':str(data),'XDG_CONFIG_HOME':str(config)}
   run('bash',ROOT/'scripts/install-linux.sh','--package-root',package,'--migrate-legacy',env=env)
   self.assertEqual((plugins/'bacons-helper/bacons-helper.so').read_text(),'new-module');self.assertFalse(legacy.exists());self.assertEqual((settings/'companion.ini').read_text(),'keep-credentials');self.assertEqual((other/'keep').read_text(),'safe');self.assertEqual(len(list((data/'obs-studio/plugin-backups').rglob('old-module'))),1)
   (source/'bacons-helper.so').write_text('updated-module');run('bash',ROOT/'scripts/install-linux.sh','--package-root',package,env=env)
   self.assertEqual((plugins/'bacons-helper/bacons-helper.so').read_text(),'updated-module');self.assertTrue(any(p.read_text()=='new-module' for p in (data/'obs-studio/plugin-backups').rglob('bacons-helper.so')))
   (source/'data/manifest.json').unlink();result=subprocess.run(['bash',str(ROOT/'scripts/install-linux.sh'),'--package-root',str(package)],env=env,capture_output=True)
   self.assertNotEqual(result.returncode,0);self.assertEqual((plugins/'bacons-helper/bacons-helper.so').read_text(),'updated-module');self.assertFalse(list(plugins.glob('.bacons-helper-stage-*')))
 def test_release_scripts_use_modern_layout(self):
  windows=(ROOT/'scripts/package-windows.ps1').read_text();self.assertIn('bacons-helper\\bacons-helper.dll',windows);self.assertIn('bacons-helper\\data\\qt-plugins\\tls',windows);self.assertIn('bacons-helper\\data\\manifest.json',windows);self.assertIn('-DBH_OBS_INSTALL_LAYOUT=BUNDLE',windows)
  linux=(ROOT/'scripts/package-linux.sh').read_text();self.assertIn('-DBH_OBS_INSTALL_LAYOUT=BUNDLE',linux);self.assertIn('/bacons-helper/data/manifest.json',linux)
  flatpak=(ROOT/'flatpak/com.obsproject.Studio.Plugin.BaconsHelper.yml').read_text();self.assertIn('org.freedesktop.Sdk//26.08',flatpak);self.assertIn('-DBH_OBS_INSTALL_LAYOUT=FLATPAK',flatpak)
if __name__=='__main__':unittest.main()
