const path=require('node:path'),fs=require('node:fs');
const {chromium,expect}=require(path.resolve(__dirname,'../../apps/manager/node_modules/@playwright/test'));
(async()=>{
 const browser=await chromium.launch({headless:true}),results=[];
 for(const [name,viewport] of [['desktop',{width:1440,height:900}],['phone',{width:390,height:844}]]){
  const page=await browser.newPage({viewport});await page.goto(process.env.ARDOR_MANAGER_BASE_URL || 'http://127.0.0.1:4189');
  await page.getByRole('button',{name:'Add block',exact:true}).filter({visible:true}).first().click();
  await page.getByRole('searchbox',{name:'Search blocks'}).fill('Hardware_FiLM_amp');
  const module=page.getByRole('button',{name:/Hardware_FiLM_amp/});await expect(module).toHaveCount(1);await expect(module).toHaveClass(/fam-amp/);
  if(await page.getByRole('group',{name:/^JCM800,/}).count()){
   await expect(module).toBeDisabled();await page.keyboard.press('Escape');
   await page.getByRole('group',{name:/^JCM800,/}).click();await page.getByRole('button',{name:'Block on',exact:true}).click();
   await page.getByRole('button',{name:'Add block',exact:true}).filter({visible:true}).first().click();
   await page.getByRole('searchbox',{name:'Search blocks'}).fill('Hardware_FiLM_amp');
  }
  await expect(module).toBeEnabled();await module.click();await page.waitForTimeout(450);
  const card=page.getByRole('group',{name:/^Hardware_FiLM_amp,/});
  if(await card.count())await card.click();else await page.locator('button.chip').filter({hasText:'Hardware_FiLM_amp'}).click();
  await expect(page.getByRole('slider',{name:'Drive',exact:true})).toHaveAttribute('aria-valuenow','5');
  await expect(page.getByRole('slider',{name:'Tone',exact:true})).toHaveAttribute('aria-valuenow','0');
  await page.getByRole('slider',{name:'Drive',exact:true}).scrollIntoViewIfNeeded();await page.waitForTimeout(450);
  await page.screenshot({path:path.join(__dirname,`manager-amp-${name}.png`),fullPage:true});
  results.push({viewport:name,passed:true,gearType:'amp',saved:false,applied:false});await page.close();
 }
 await browser.close();fs.writeFileSync(path.join(__dirname,'amp-browser-results.json'),JSON.stringify(results,null,2)+'\n');
})().catch(e=>{console.error(e);process.exit(1)});
