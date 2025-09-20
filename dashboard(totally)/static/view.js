// Smart Voting Dashboard - Advanced Version
// Enhanced with real-time charts, activity feed, and better UX

// DOM Elements
const statusText = document.getElementById('status-text');
const statusDot = document.getElementById('status-dot');
const currentTime = document.getElementById('current-time');
const totalVotes = document.getElementById('total-votes');
const leadingCandidate = document.getElementById('leading-candidate');
const votesPerMinute = document.getElementById('votes-per-minute');
const lastUpdate = document.getElementById('last-update');
const candidatesGrid = document.getElementById('candidates-grid');
const trendChart = document.getElementById('trend-chart');
const activityChart = document.getElementById('activity-chart');
const activityFeed = document.getElementById('activity-feed');

// State
let pollMs = 1000;
let labels = {};
let photos = {};
let lastHistory = [];
let voteHistory = [];
let activityLog = [];
let lastTotalVotes = 0;
let startTime = Date.now();

// Candidate names (matching ESP32)
const NAMES = [
  "งดออกเสียง","ผู้สมัคร 1","ผู้สมัคร 2","ผู้สมัคร 3","ผู้สมัคร 4",
  "ผู้สมัคร 5","ผู้สมัคร 6","ผู้สมัคร 7","ผู้สมัคร 8","ผู้สมัคร 9"
];

// Candidate photos (provided URLs)
const PHOTOS = [
  "https://mercyinvestmentservices.org/wp-content/uploads/AdobeStock_70618933-scaled.jpeg",
  "https://upload.wikimedia.org/wikipedia/commons/thumb/e/e9/Field_Marshal_Plaek_Phibunsongkhram.jpg/330px-Field_Marshal_Plaek_Phibunsongkhram.jpg",
  "https://upload.wikimedia.org/wikipedia/commons/thumb/6/64/Anutin_Charnvirakul_in_September_2025_%28cropped%29.png/330px-Anutin_Charnvirakul_in_September_2025_%28cropped%29.png",
  "https://upload.wikimedia.org/wikipedia/commons/thumb/7/7b/Anand_Panyarachun_%28cropped%29.jpg/250px-Anand_Panyarachun_%28cropped%29.jpg",
  "https://upload.wikimedia.org/wikipedia/commons/5/58/Chuan_Likphai_1993_cropped.jpg",
  "https://upload.wikimedia.org/wikipedia/commons/thumb/b/b6/The_Prime_Minister_of_Thailand%2C_Mr._Somchai_Wangsawat_meeting_the_Prime_Minister%2C_Dr._Manmohan_Singh%2C_in_New_Delhi_on_November_13%2C_2008_%28cropped%29_%28cropped%29.jpg/250px-The_Prime_Minister_of_Thailand%2C_Mr._Somchai_Wangsawat_meeting_the_Prime_Minister%2C_Dr._Manmohan_Singh%2C_in_New_Delhi_on_November_13%2C_2008_%28cropped%29_%28cropped%29.jpg",
  "https://upload.wikimedia.org/wikipedia/commons/thumb/2/28/Abhisit_Vejjajiva_2010.jpg/250px-Abhisit_Vejjajiva_2010.jpg",
  "https://upload.wikimedia.org/wikipedia/commons/thumb/2/2b/Prawit_Wongsuwan_%282018%29_cropped.jpg/250px-Prawit_Wongsuwan_%282018%29_cropped.jpg",
  "https://upload.wikimedia.org/wikipedia/commons/thumb/d/d4/Phumtham_Wechayachai_30_May_2025.jpg/250px-Phumtham_Wechayachai_30_May_2025.jpg",
  "https://upload.wikimedia.org/wikipedia/commons/thumb/4/4d/Kriangsak_Chomanan_1976_%28cropped%29.jpg/250px-Kriangsak_Chomanan_1976_%28cropped%29.jpg"
];

// Utility functions
function fmt(n) { return n.toLocaleString(); }
function el(t, c) { const e = document.createElement(t); if(c) e.className = c; return e; }
function formatTime(date) { return date.toLocaleTimeString('th-TH', {hour: '2-digit', minute: '2-digit'}); }

// Initialize candidate cards
function initializeCandidates() {
  candidatesGrid.innerHTML = '';
  for(let i = 0; i < 10; i++) {
    const card = createCandidateCard(i, 0, 1);
    candidatesGrid.appendChild(card);
  }
}

// Create candidate card
function createCandidateCard(i, votes, maxVotes) {
  const label = labels[i] || NAMES[i] || `ผู้สมัคร ${i}`;
  const photo = PHOTOS[i] || '';
  const percentage = maxVotes > 0 ? Math.round((votes * 100) / maxVotes) : 0;
  
  const card = el('div', 'candidate-card');
  card.innerHTML = `
    <img class="candidate-photo" src="${photo}" alt="${label}" onerror="this.style.display='none'">
    <div class="candidate-name">${label} (${i})</div>
    <div class="candidate-votes" id="votes-${i}">${fmt(votes)}</div>
    <div class="candidate-percentage">${percentage}% ของคะแนนสูงสุด</div>
    <div class="progress-bar">
      <div class="progress-fill" id="progress-${i}" style="width: ${percentage}%"></div>
    </div>
  `;
  return card;
}

// Update current time
function updateCurrentTime() {
  const now = new Date();
  const timeString = now.toLocaleString('th-TH', {
    year: 'numeric',
    month: 'long',
    day: 'numeric',
    hour: '2-digit',
    minute: '2-digit',
    second: '2-digit'
  });
  if(currentTime) currentTime.textContent = timeString;
}

// Update candidate cards
function updateCandidates(counts, maxVotes) {
  let leadingCandidateId = null;
  let maxVotesCount = 0;
  
  for(let i = 0; i < 10; i++) {
    const votes = counts[i] || 0;
    const percentage = maxVotes > 0 ? Math.round((votes * 100) / maxVotes) : 0;
    
    const votesEl = document.getElementById(`votes-${i}`);
    const progressEl = document.getElementById(`progress-${i}`);
    const cardEl = document.querySelector(`#candidates-grid .candidate-card:nth-child(${i + 1})`);
    
    if(votesEl) votesEl.textContent = fmt(votes);
    if(progressEl) progressEl.style.width = `${percentage}%`;
    
    // Remove leading class from all cards
    if(cardEl) cardEl.classList.remove('leading');
    
    // Track leading candidate
    if(votes > maxVotesCount) {
      maxVotesCount = votes;
      leadingCandidateId = i;
    }
  }
  
  // Add leading class to the candidate with most votes
  if(leadingCandidateId !== null && maxVotesCount > 0) {
    const leadingCard = document.querySelector(`#candidates-grid .candidate-card:nth-child(${leadingCandidateId + 1})`);
    if(leadingCard) leadingCard.classList.add('leading');
  }
}

// Draw trend chart
function drawTrendChart(history) {
  const ctx = trendChart.getContext('2d');
  const W = trendChart.clientWidth;
  const H = trendChart.clientHeight;
  
  trendChart.width = W * devicePixelRatio;
  trendChart.height = H * devicePixelRatio;
  ctx.scale(devicePixelRatio, devicePixelRatio);
  ctx.clearRect(0, 0, W, H);

  if(history.length < 2) return;

  const totals = history.map(p => p.total);
  const min = Math.min(...totals);
  const max = Math.max(...totals);
  const range = max - min || 1;
  
  const pad = 20;
  const chartW = W - pad * 2;
  const chartH = H - pad * 2;
  
  // Draw grid lines
  ctx.strokeStyle = '#1f2937';
  ctx.lineWidth = 1;
  for(let i = 0; i <= 5; i++) {
    const y = pad + (chartH / 5) * i;
    ctx.beginPath();
    ctx.moveTo(pad, y);
    ctx.lineTo(W - pad, y);
    ctx.stroke();
  }

  // Draw trend line
  ctx.strokeStyle = '#22d3ee';
  ctx.lineWidth = 3;
  ctx.beginPath();
  
  for(let i = 0; i < history.length; i++) {
    const x = pad + (chartW / (history.length - 1)) * i;
    const y = H - pad - ((history[i].total - min) / range) * chartH;
    
    if(i === 0) ctx.moveTo(x, y);
    else ctx.lineTo(x, y);
  }
  ctx.stroke();

  // Draw gradient fill
  const gradient = ctx.createLinearGradient(0, 0, 0, H);
  gradient.addColorStop(0, 'rgba(34, 211, 238, 0.3)');
  gradient.addColorStop(1, 'rgba(34, 211, 238, 0.05)');
  ctx.fillStyle = gradient;
  ctx.lineTo(W - pad, H - pad);
  ctx.lineTo(pad, H - pad);
  ctx.closePath();
  ctx.fill();
}

// Draw activity chart (24-hour heatmap)
function drawActivityChart() {
  const ctx = activityChart.getContext('2d');
  const W = activityChart.clientWidth;
  const H = activityChart.clientHeight;
  
  activityChart.width = W * devicePixelRatio;
  activityChart.height = H * devicePixelRatio;
  ctx.scale(devicePixelRatio, devicePixelRatio);
  ctx.clearRect(0, 0, W, H);

  // Create 24-hour activity data (simulate for demo)
  const hours = Array.from({length: 24}, (_, i) => {
    const now = new Date();
    const hour = new Date(now.getFullYear(), now.getMonth(), now.getDate(), i);
    const activity = Math.floor(Math.random() * 20) + 1; // Simulate activity
    return { hour: i, activity, time: hour };
  });

  const maxActivity = Math.max(...hours.map(h => h.activity));
  const barWidth = W / 24;
  
  hours.forEach((hour, i) => {
    const barHeight = (hour.activity / maxActivity) * (H - 40);
    const x = i * barWidth;
    const y = H - barHeight - 20;
    
    // Color intensity based on activity
    const intensity = hour.activity / maxActivity;
    const color = `rgba(34, 211, 238, ${0.3 + intensity * 0.7})`;
    
    ctx.fillStyle = color;
    ctx.fillRect(x + 2, y, barWidth - 4, barHeight);
    
    // Hour labels
    if(i % 4 === 0) {
      ctx.fillStyle = '#94a3b8';
      ctx.font = '10px system-ui';
      ctx.textAlign = 'center';
      ctx.fillText(`${hour.hour}:00`, x + barWidth/2, H - 5);
    }
  });
}

// Update activity feed
function updateActivityFeed(newVotes) {
  const now = new Date();
  
  // Add new activities
  Object.entries(newVotes).forEach(([candidate, votes]) => {
    if(votes > 0) {
      const label = labels[candidate] || NAMES[candidate] || `ผู้สมัคร ${candidate}`;
      activityLog.unshift({
        time: now,
        text: `โหวตให้ ${label}`,
        candidate: candidate
      });
    }
  });
  
  // Keep only last 10 activities
  activityLog = activityLog.slice(0, 10);
  
  // Update UI
  activityFeed.innerHTML = activityLog.map(activity => `
    <div class="activity-item">
      <div class="activity-time">${formatTime(activity.time)}</div>
      <div class="activity-text">${activity.text}</div>
      <div class="activity-badge">${activity.candidate}</div>
    </div>
  `).join('');
}

// Calculate votes per minute
function calculateVotesPerMinute() {
  const now = Date.now();
  const minutesElapsed = (now - startTime) / (1000 * 60);
  return minutesElapsed > 0 ? Math.round(lastTotalVotes / minutesElapsed) : 0;
}

// Find leading candidate
function findLeadingCandidate(counts) {
  let maxVotes = 0;
  let leading = '-';
  
  Object.entries(counts).forEach(([candidate, votes]) => {
    if(votes > maxVotes) {
      maxVotes = votes;
      leading = labels[candidate] || NAMES[candidate] || `ผู้สมัคร ${candidate}`;
    }
  });
  
  return leading;
}

// Main update function
async function updateDashboard() {
  try {
    const response = await fetch('/tally', {cache: 'no-store'});
    if(!response.ok) throw new Error('Network error');
    
    const data = await response.json();
    
    // Update stats
    totalVotes.textContent = fmt(data.total);
    leadingCandidate.textContent = findLeadingCandidate(data.counts);
    votesPerMinute.textContent = calculateVotesPerMinute();
    lastUpdate.textContent = formatTime(new Date());
    
    // Update candidates
    updateCandidates(data.counts, data.max);
    
    // Track vote changes for activity feed
    if(data.total > lastTotalVotes) {
      const newVotes = {};
      Object.entries(data.counts).forEach(([candidate, votes]) => {
        const oldVotes = lastHistory.length > 0 ? 
          (lastHistory[lastHistory.length - 1].counts[candidate] || 0) : 0;
        if(votes > oldVotes) {
          newVotes[candidate] = votes - oldVotes;
        }
      });
      updateActivityFeed(newVotes);
    }
    
    // Update charts
    const history = await fetch('/history', {cache: 'no-store'})
      .then(r => r.json())
      .catch(() => []);
    
    lastHistory = history;
    drawTrendChart(history);
    drawActivityChart();
    
    // Update status
    statusText.textContent = 'Online';
    statusDot.className = 'status-dot online';
    lastTotalVotes = data.total;
    
  } catch(error) {
    statusText.textContent = 'Offline';
    statusDot.className = 'status-dot offline';
    console.error('Dashboard update error:', error);
  }
}

// Initialize dashboard
async function initDashboard() {
  // Initialize with default names
  for(let i = 0; i < 10; i++) {
    labels[i] = NAMES[i];
  }
  
  // Initialize UI
  initializeCandidates();
  drawActivityChart();
  updateCurrentTime();
  
  // Start time updates
  setInterval(updateCurrentTime, 1000);
  
  // Start polling
  await updateDashboard();
  setInterval(updateDashboard, pollMs);
}

// Start the dashboard
document.addEventListener('DOMContentLoaded', initDashboard);