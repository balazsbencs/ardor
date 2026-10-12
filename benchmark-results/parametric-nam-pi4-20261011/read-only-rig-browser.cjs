const path=require('node:path');
const {chromium,expect}=require(path.resolve(__dirname,'../../apps/manager/node_modules/@playwright/test'));
const fs=require('node:fs');
const base=process.env.ARDOR_MANAGER_BASE_URL || 'http://127.0.0.1:4189';
(async()=>{
 const browser=await chromium.launch({headless:true});
 const results=[];
 for(const [name,viewport] of [['desktop',{width:1440,height:900}],['phone',{width:390,height:844}]]){
  const page=await browser.newPage({viewport});await page.goto(base);
  await expect(page.getByRole('textbox',{name:'Preset name'})).toHaveValue('mixed-clone-0-1');
  await page.getByRole('group',{name:/^clone-overdrive,/}).click();
  await expect(page.getByRole('slider',{name:'Gain',exact:true})).toHaveAttribute('aria-valuenow','0.5');
  await expect(page.getByRole('slider',{name:'Tone',exact:true})).toHaveAttribute('aria-valuenow','0.5');
  await expect(page.getByRole('radiogroup',{name:'Model size'}).getByRole('radio',{name:'Nano',exact:true})).toHaveAttribute('aria-checked','true');
  await page.getByRole('slider',{name:'Gain',exact:true}).scrollIntoViewIfNeeded();
  await page.waitForTimeout(450);
  await page.screenshot({path:path.join(__dirname,`live-rig-${name}.png`),fullPage:true});
  results.push({viewport:name,passed:true,readOnly:true});await page.close();
 }
 await browser.close();fs.writeFileSync(path.join(__dirname,'read-only-browser-results.json'),JSON.stringify(results,null,2)+'\n');
})().catch(e=>{console.error(e);process.exit(1)});
