const assert = require('node:assert/strict');
const {test,before} = require('node:test');
const {execFileSync} = require('node:child_process');
const path = require('node:path');
const routing = require('./tree-routing.js');
before(async()=>{
 const runtime = execFileSync('python3',[path.join(__dirname,'build_tree_router.py')],{encoding:'utf8'}).trim();
 global.createTreeConnectionRouter = require(runtime);
 await routing.ready();
});
function avoids(rect,a,b) {
 return a[0]===b[0] ? !(a[0]>rect[0]&&a[0]<rect[2]&&Math.min(a[1],b[1])<rect[3]&&Math.max(a[1],b[1])>rect[1]) :
  !(a[1]>rect[1]&&a[1]<rect[3]&&Math.min(a[0],b[0])<rect[2]&&Math.max(a[0],b[0])>rect[0]);
}
test('same-parent siblings reuse one trunk and fork in the browser',()=>{
 const nodes=[[125,25,175,75],[45,155,95,205],[205,155,255,205]];
 const paths=routing.plan(nodes,[[0,1],[0,2]],[],[8,8,292,240]);
 for(const route of paths){
  assert.deepEqual(route.points.slice(0,2),[[150,75],[150,115]]);
  assert.equal(route.points[2][1],115);
  assert.equal(route.strokes.length,1);
 }
});
test('browser uses the game router to bypass intermediate nodes and labels',()=>{
 const nodes=[[125,25,175,75],[125,135,175,185],[125,245,175,295]], labels=[[105,190,195,210]];
 const paths=routing.plan(nodes,[[0,2],[0,1],[1,2]],labels,[8,8,292,330]);
 assert.ok(paths[0].points.length>2);
 for(const route of paths){
  assert.ok(route.strokes.length);
  for(let i=1;i<route.points.length;i++) for(const rect of [...nodes,...labels]) assert.ok(avoids(rect,route.points[i-1],route.points[i]));
 }
});
test('each dependency survives a dense graph and horizontal defense branches',()=>{
 const nodes=[[25,90,65,130],[125,90,165,130],[225,90,265,130]], edges=[[0,1],[1,2],[0,2]];
 const paths=routing.plan(nodes,edges,[],[8,8,292,220]);
 assert.equal(paths.length,edges.length);
 assert.ok(paths.every(route=>route.points.length>=2&&route.strokes.length));
 assert.ok(routing.plan(nodes,[[-1,2],[0,99],[0,0]],[],[8,8,292,220]).every(route=>!route.points.length));
});
