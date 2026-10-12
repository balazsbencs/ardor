const path=require('node:path');
const {chromium,expect}=require(path.resolve(__dirname,'../../apps/manager/node_modules/@playwright/test'));
const fs=require('node:fs');
const root=__dirname;
const base=process.env.ARDOR_MANAGER_BASE_URL || 'http://127.0.0.1:4189';
async function request(path,options){const response=await fetch(base+path,options); if(!response.ok) throw new Error(`${path}: ${response.status} ${await response.text()}`);return response.status===204?null:response.json();}
async function activate(){const result=await request('/api/presets/banks/0/slots/0/apply',{method:'POST'}); for(let i=0;i<600;i++){const status=await request('/api/runtime/apply/'+result.id);if(status.state!=='pending'){if(status.state!=='applied')throw new Error(JSON.stringify(status));return;}await new Promise(r=>setTimeout(r,100));}throw new Error('apply timeout');}
(async()=>{
 const browser=await chromium.launch({headless:true});
 const observations=[];
 for(const [viewportName,viewport] of [['desktop',{width:1440,height:900}],['phone',{width:390,height:844}]]) {
  const page=await browser.newPage({viewport});
  const errors=[];page.on('pageerror',e=>errors.push(e.message));
  for(const [kind,stem,first,second,isSwitch,hasTier] of [
   ['clone','Hardware_renamed_clone','Gain','Tone',false,true],
   ['concat','Hardware_a2-dual-0','Drive','Mode',true,true],
   ['film','Hardware_film-dual-0','Drive','Tone',false,true],
   ['synthetic','Hardware_Ardor-Parametric-Overdrive-Demo','Drive','Tone',false,false]]) {
   const empty={version:1,name:'Hardware browser '+kind,routing:'serial',global:{inputGainDb:0,outputGainDb:-30,safetyLimitDb:-1},blocks:[]};
   await request('/api/presets/banks/0/slots/0',{method:'PUT',headers:{'Content-Type':'application/json'},body:JSON.stringify(empty)});await activate();
   await page.goto(base);
   await expect(page.getByRole('textbox',{name:'Preset name'})).toHaveValue(empty.name,{timeout:30000});
   await page.getByRole('button',{name:'Add block',exact:true}).filter({visible:true}).first().click();
   await page.getByRole('searchbox',{name:'Search blocks'}).fill(stem);
   const module=page.getByRole('button',{name:new RegExp(stem)});await expect(module).toHaveCount(1);await module.click();
   await page.getByRole('group',{name:new RegExp('^'+stem+',')}).click();
   await page.getByRole('slider',{name:first,exact:true}).press('End');
   if(isSwitch) await page.getByRole('radiogroup',{name:second,exact:true}).getByRole('radio',{name:'Hard',exact:true}).click();
   else await page.getByRole('slider',{name:second,exact:true}).press('Home');
   if(hasTier) await page.getByRole('radiogroup',{name:'Model size'}).getByRole('radio',{name:'Nano',exact:true}).click();
   await page.getByRole('button',{name:'Save',exact:true}).filter({visible:true}).click();
   await expect.poll(async()=> (await request('/api/presets/banks/0/slots/0')).preset.blocks.length).toBe(1);
   const saved=(await request('/api/presets/banks/0/slots/0')).preset;
   const params=saved.blocks[0].params;
   const assets=(await request('/api/assets/models')).assets;
   const asset=assets.find(a=>a.path===saved.blocks[0].asset);
   const specs=asset.nam.parameters;
   expect(params.modelParameters[first]).toBe(specs.find(p=>p.name===first).max);
   expect(params.modelParameters[second]).toBe(isSwitch?1:specs.find(p=>p.name===second).min);
   if(hasTier) expect(params.useNano).toBe(true);
   await activate();
   await page.reload();await page.getByRole('group',{name:new RegExp('^'+stem+',')}).click();
   await expect(page.getByRole('slider',{name:first,exact:true})).toHaveAttribute('aria-valuenow',String(params.modelParameters[first]));
   if(hasTier) await expect(page.getByRole('radiogroup',{name:'Model size'}).getByRole('radio',{name:'Nano',exact:true})).toHaveAttribute('aria-checked','true');
   const overflow=await page.evaluate(()=>document.documentElement.scrollWidth>window.innerWidth);expect(overflow).toBe(false);
   await page.waitForTimeout(450); // Let the 320 ms view transition finish before capture.
   await page.screenshot({path:`${root}/manager-${kind}-${viewportName}.png`,fullPage:true});
   observations.push({viewport:viewportName,kind,passed:true,params});console.log('PASS',viewportName,kind,JSON.stringify(params));
  }
  expect(errors).toEqual([]);await page.close();
 }
 await browser.close();fs.writeFileSync(root+'/browser-results.json',JSON.stringify({passed:true,observations},null,2)+'\n');
})().catch(error=>{console.error(error);process.exit(1);});
